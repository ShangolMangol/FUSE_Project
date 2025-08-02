/*
 * Copyright 2016 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "guetzli/jpeg_data_writer.h"
#include "guetzli/jpeg_data_reader.h"

#include <assert.h>
#include <cstdlib>
#include <string.h>
#include <cstdio>
#include <vector>
#include <string>
#include <cstdint>

#include "guetzli/entropy_encode.h"
#include "guetzli/fast_log.h"
#include "guetzli/jpeg_bit_writer.h"
#include "guetzli/jpeg_huffman_decode.h"

namespace guetzli {

// This callback is used when merging to write to a std::string.
static int GuetzliStringOut(void* data, const uint8_t* buf, size_t len) {
  std::string* out = reinterpret_cast<std::string*>(data);
  out->append(reinterpret_cast<const char*>(buf), len);
  return len; // Return number of bytes written
}

// -- Definitions for SimpleBitWriter --
void SimpleBitWriter::WriteBits(uint32_t bits, int nbits) {
    for (int i = nbits - 1; i >= 0; --i) {
      cur_byte = (cur_byte << 1) | ((bits >> i) & 1);
      bit_pos++;
      if (bit_pos == 8) {
        data.push_back(cur_byte);
        cur_byte = 0;
        bit_pos = 0;
      }
    }
}
void SimpleBitWriter::Flush() {
    if (bit_pos > 0) {
      cur_byte <<= (8 - bit_pos);
      data.push_back(cur_byte);
      cur_byte = 0;
      bit_pos = 0;
    }
}
void SimpleBitWriter::WriteToFile(FILE* f) {
    Flush();
    fprintf(stderr, "Writing %zu bytes to file\n", data.size());
    size_t written = fwrite(data.data(), 1, data.size(), f);
    fprintf(stderr, "Actually written: %zu bytes\n", written);
    if (written != data.size()) {
        fprintf(stderr, "ERROR: Expected to write %zu bytes but wrote %zu\n", data.size(), written);
    }
}

// -- Definition for SimpleBitReader --
uint32_t SimpleBitReader::ReadBits(int nbits) {
    uint32_t val = 0;
    for (int i = 0; i < nbits; ++i) {
      if (byte_pos >= size) return 0; // error
      val = (val << 1) | ((data[byte_pos] >> (7 - bit_pos)) & 1);
      bit_pos++;
      if (bit_pos == 8) {
        bit_pos = 0;
        byte_pos++;
      }
    }
    return val;
}

// -- Definitions for JpegHistogram --
JpegHistogram::JpegHistogram() { Clear(); }
void JpegHistogram::Clear() {
  memset(counts, 0, sizeof(counts));
  counts[kSize - 1] = 1;
}
void JpegHistogram::Add(int symbol) {
  counts[symbol] += 2;
}
void JpegHistogram::Add(int symbol, int weight) {
  counts[symbol] += 2 * weight;
}
void JpegHistogram::AddHistogram(const JpegHistogram& other) {
  for (int i = 0; i + 1 < kSize; ++i) {
    counts[i] += other.counts[i];
  }
  counts[kSize - 1] = 1;
}
int JpegHistogram::NumSymbols() const {
  int n = 0;
  for (int i = 0; i + 1 < kSize; ++i) {
    n += (counts[i] > 0 ? 1 : 0);
  }
  return n;
}

// -- Start of internal (anonymous namespace) helper functions --
namespace {

static const int kJpegPrecision = 8;

inline bool JPEGWrite(JPEGOutput out, const uint8_t* buf, size_t len) {
  return out.Write(buf, len);
}

inline bool JPEGWrite(JPEGOutput out, const std::string& s) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(&s[0]);
  return JPEGWrite(out, data, s.size());
}

bool EncodeMetadata(const JPEGData& jpg, bool strip_metadata, JPEGOutput out) {
  if (strip_metadata) {
    const uint8_t kApp0Data[] = {
      0xff, 0xe0, 0x00, 0x10,        // APP0
      0x4a, 0x46, 0x49, 0x46, 0x00,  // 'JFIF'
      0x01, 0x01,                    // v1.01
      0x00, 0x00, 0x01, 0x00, 0x01,  // aspect ratio = 1:1
      0x00, 0x00                     // thumbnail width/height
    };
    return JPEGWrite(out, kApp0Data, sizeof(kApp0Data));
  }
  bool ok = true;
  for (size_t i = 0; i < jpg.app_data.size(); ++i) {
    uint8_t data[1] = { 0xff };
    ok = ok && JPEGWrite(out, data, sizeof(data));
    ok = ok && JPEGWrite(out, jpg.app_data[i]);
  }
  for (size_t i = 0; i < jpg.com_data.size(); ++i) {
    uint8_t data[2] = { 0xff, 0xfe };
    ok = ok && JPEGWrite(out, data, sizeof(data));
    ok = ok && JPEGWrite(out, jpg.com_data[i]);
  }
  return ok;
}

bool EncodeDQT(const std::vector<JPEGQuantTable>& quant, JPEGOutput out) {
  int marker_len = 2;
  for (size_t i = 0; i < quant.size(); ++i) {
    marker_len += 1 + (quant[i].precision ? 2 : 1) * kDCTBlockSize;
  }
  std::vector<uint8_t> data(marker_len + 2);
  size_t pos = 0;
  data[pos++] = 0xff;
  data[pos++] = 0xdb;
  data[pos++] = marker_len >> 8;
  data[pos++] = marker_len & 0xff;
  for (size_t i = 0; i < quant.size(); ++i) {
    const JPEGQuantTable& table = quant[i];
    data[pos++] = (table.precision << 4) + table.index;
    for (int k = 0; k < kDCTBlockSize; ++k) {
      int val = table.values[kJPEGNaturalOrder[k]];
      if (table.precision) {
        data[pos++] = val >> 8;
      }
      data[pos++] = val & 0xff;
    }
  }
  return JPEGWrite(out, &data[0], pos);
}

bool EncodeSOF(const JPEGData& jpg, JPEGOutput out) {
  const size_t ncomps = jpg.components.size();
  const size_t marker_len = 8 + 3 * ncomps;
  std::vector<uint8_t> data(marker_len + 2);
  size_t pos = 0;
  data[pos++] = 0xff;
  data[pos++] = 0xc1;
  data[pos++] = static_cast<uint8_t>(marker_len >> 8);
  data[pos++] = marker_len & 0xff;
  data[pos++] = kJpegPrecision;
  data[pos++] = jpg.height >> 8;
  data[pos++] = jpg.height & 0xff;
  data[pos++] = jpg.width >> 8;
  data[pos++] = jpg.width & 0xff;
  data[pos++] = static_cast<uint8_t>(ncomps);
  for (size_t i = 0; i < ncomps; ++i) {
    data[pos++] = jpg.components[i].id;
    data[pos++] = ((jpg.components[i].h_samp_factor << 4) |
                      (jpg.components[i].v_samp_factor));
    const size_t quant_idx = jpg.components[i].quant_idx;
    if (quant_idx >= jpg.quant.size()) {
      return false;
    }
    data[pos++] = jpg.quant[quant_idx].index;
  }
  return JPEGWrite(out, &data[0], pos);
}

void BuildHuffmanCode(uint8_t* depth, int* counts, int* values) {
  for (int i = 0; i < JpegHistogram::kSize; ++i) {
    if (depth[i] > 0) {
      ++counts[depth[i]];
    }
  }
  int offset[kJpegHuffmanMaxBitLength + 1] = { 0 };
  for (int i = 1; i <= kJpegHuffmanMaxBitLength; ++i) {
    offset[i] = offset[i - 1] + counts[i - 1];
  }
  for (int i = 0; i < JpegHistogram::kSize; ++i) {
    if (depth[i] > 0) {
      values[offset[depth[i]]++] = i;
    }
  }
}

void BuildHuffmanCodeTable(const int* counts, const int* values,
                           HuffmanCodeTable* table) {
  int huffcode[256];
  int huffsize[256];
  int p = 0;
  for (int l = 1; l <= kJpegHuffmanMaxBitLength; ++l) {
    int i = counts[l];
    while (i--) huffsize[p++] = l;
  }

  if (p == 0)
    return;

  huffsize[p - 1] = 0;
  int lastp = p - 1;

  int code = 0;
  int si = huffsize[0];
  p = 0;
  while (huffsize[p]) {
    while ((huffsize[p]) == si) {
      huffcode[p++] = code;
      code++;
    }
    code <<= 1;
    si++;
  }
  for (p = 0; p < lastp; p++) {
    int i = values[p];
    table->depth[i] = huffsize[p];
    table->code[i] = huffcode[p];
  }
}

bool BuildAndEncodeHuffmanCodes(const JPEGData& jpg, JPEGOutput out,
                                std::vector<HuffmanCodeTable>* dc_huff_tables,
                                std::vector<HuffmanCodeTable>* ac_huff_tables) {
  const int ncomps = jpg.components.size();
  dc_huff_tables->resize(ncomps);
  ac_huff_tables->resize(ncomps);
  std::vector<JpegHistogram> histograms(ncomps);
  BuildDCHistograms(jpg, &histograms[0]);
  size_t num_dc_histo = ncomps;
  int dc_histo_indexes[kMaxComponents];
  std::vector<uint8_t> depths(ncomps * JpegHistogram::kSize);
  ClusterHistograms(&histograms[0], &num_dc_histo, dc_histo_indexes,
                    &depths[0]);
  histograms.resize(num_dc_histo + ncomps);
  depths.resize((num_dc_histo + ncomps) * JpegHistogram::kSize);
  BuildACHistograms(jpg, &histograms[num_dc_histo]);
  size_t num_ac_histo = ncomps;
  int ac_histo_indexes[kMaxComponents];
  ClusterHistograms(&histograms[num_dc_histo], &num_ac_histo, ac_histo_indexes,
                    &depths[num_dc_histo * JpegHistogram::kSize]);
  int num_histo = num_dc_histo + num_ac_histo;
  histograms.resize(num_histo);
  int total_count = 0;
  for (size_t i = 0; i < histograms.size(); ++i) {
    total_count += histograms[i].NumSymbols();
  }
  const size_t dht_marker_len =
      2 + num_histo * (kJpegHuffmanMaxBitLength + 1) + total_count;
  const size_t sos_marker_len = 6 + 2 * ncomps;
  std::vector<uint8_t> data(dht_marker_len + sos_marker_len + 4);
  size_t pos = 0;
  data[pos++] = 0xff;
  data[pos++] = 0xc4;
  data[pos++] = static_cast<uint8_t>(dht_marker_len >> 8);
  data[pos++] = dht_marker_len & 0xff;
  for (int i = 0; i < num_histo; ++i) {
    const bool is_dc = static_cast<size_t>(i) < num_dc_histo;
    const int idx = is_dc ? i : i - num_dc_histo;
    int counts[kJpegHuffmanMaxBitLength + 1] = { 0 };
    int values[JpegHistogram::kSize] = { 0 };
    BuildHuffmanCode(&depths[i * JpegHistogram::kSize], counts, values);
    HuffmanCodeTable table;
    for (int j = 0; j < 256; ++j) table.depth[j] = 255;
    BuildHuffmanCodeTable(counts, values, &table);
    for (int c = 0; c < ncomps; ++c) {
      if (is_dc) {
        if (dc_histo_indexes[c] == idx) (*dc_huff_tables)[c] = table;
      } else {
        if (ac_histo_indexes[c] == idx) (*ac_huff_tables)[c] = table;
      }
    }
    int max_length = kJpegHuffmanMaxBitLength;
    while (max_length > 0 && counts[max_length] == 0) --max_length;
    --counts[max_length];
    int total_count_for_histo = 0;
    for (int j = 0; j <= max_length; ++j) total_count_for_histo += counts[j];
    data[pos++] = is_dc ? i : static_cast<uint8_t>(i - num_dc_histo + 0x10);
    for (size_t j = 1; j <= kJpegHuffmanMaxBitLength; ++j) {
      data[pos++] = counts[j];
    }
    for (int j = 0; j < total_count_for_histo; ++j) {
      data[pos++] = values[j];
    }
  }
  data[pos++] = 0xff;
  data[pos++] = 0xda;
  data[pos++] = static_cast<uint8_t>(sos_marker_len >> 8);
  data[pos++] = sos_marker_len & 0xff;
  data[pos++] = ncomps;
  for (int i = 0; i < ncomps; ++i) {
    data[pos++] = jpg.components[i].id;
    data[pos++] = (dc_histo_indexes[i] << 4) | ac_histo_indexes[i];
  }
  data[pos++] = 0;
  data[pos++] = 63;
  data[pos++] = 0;
  assert(pos == data.size());
  return JPEGWrite(out, &data[0], data.size());
}

void EncodeDCTBlockSequential(const coeff_t* coeffs,
                              const HuffmanCodeTable& dc_huff,
                              const HuffmanCodeTable& ac_huff,
                              coeff_t* last_dc_coeff,
                              BitWriter* bw,
                              const SplitMergeOptions* split_merge_opts,
                              void* noncrit_bits) {
  coeff_t temp = coeffs[0] - *last_dc_coeff;
  *last_dc_coeff = coeffs[0];
  coeff_t temp2 = temp;
  if (temp < 0) {
    temp = -temp;
    temp2--;
  }
  int nbits = (temp == 0) ? 0 : Log2Floor(temp) + 1;
  bw->WriteBits(dc_huff.depth[nbits], dc_huff.code[nbits]);
  if (nbits > 0) {
    bw->WriteBits(nbits, temp2 & ((1 << nbits) - 1));
  }
  int r = 0;
  if (split_merge_opts && split_merge_opts->split_jpeg && noncrit_bits) {
    SimpleBitWriter* writer = reinterpret_cast<SimpleBitWriter*>(noncrit_bits);
    WriteACBitsToNoncrit(coeffs, writer);
    for (int k = 1; k < 64; ++k) {
      coeff_t coeff = coeffs[kJPEGNaturalOrder[k]];
      if (coeff == 0) {
        r++;
        continue;
      }
      while (r > 15) {
        bw->WriteBits(ac_huff.depth[0xf0], ac_huff.code[0xf0]); // ZRL
        r -= 16;
      }
      int ac_nbits = Log2FloorNonZero(std::abs(coeff)) + 1;
      int symbol = (r << 4) + ac_nbits;
      bw->WriteBits(ac_huff.depth[symbol], ac_huff.code[symbol]);
      bw->WriteBits(ac_nbits, 0); // Write ZEROES for value bits.
      r = 0;
    }
  } else {
    for (int k = 1; k < 64; ++k) {
      coeff_t coeff = coeffs[kJPEGNaturalOrder[k]];
      if (coeff == 0) {
        r++;
        continue;
      }
      while (r > 15) {
        bw->WriteBits(ac_huff.depth[0xf0], ac_huff.code[0xf0]); // ZRL
        r -= 16;
      }
      temp2 = coeff;
      if (temp2 < 0) {
        temp2 = -temp2;
        temp2 = ~temp2;
      }
      int ac_nbits = Log2FloorNonZero(std::abs(coeff)) + 1;
      int symbol = (r << 4) + ac_nbits;
      bw->WriteBits(ac_huff.depth[symbol], ac_huff.code[symbol]);
      bw->WriteBits(ac_nbits, temp2 & ((1 << ac_nbits) - 1));
      r = 0;
    }
  }
  if (r > 0) { // EOB
    bw->WriteBits(ac_huff.depth[0], ac_huff.code[0]);
  }
}

bool EncodeScan(const JPEGData& jpg,
                const std::vector<HuffmanCodeTable>& dc_huff_table,
                const std::vector<HuffmanCodeTable>& ac_huff_table,
                JPEGOutput out,
                const SplitMergeOptions* split_merge_opts) {
  coeff_t last_dc_coeff[kMaxComponents] = {0};
  BitWriter bw(1 << 17);
  SimpleBitWriter noncrit_writer;
  void* noncrit_bits_ptr = nullptr;
  if (split_merge_opts && split_merge_opts->split_jpeg) {
    noncrit_bits_ptr = &noncrit_writer;
  }
  for (int mcu_y = 0; mcu_y < jpg.MCU_rows; ++mcu_y) {
    for (int mcu_x = 0; mcu_x < jpg.MCU_cols; ++mcu_x) {
      for (size_t i = 0; i < jpg.components.size(); ++i) {
        const JPEGComponent& c = jpg.components[i];
        for (int iy = 0; iy < c.v_samp_factor; ++iy) {
          for (int ix = 0; ix < c.h_samp_factor; ++ix) {
            int block_y = mcu_y * c.v_samp_factor + iy;
            int block_x = mcu_x * c.h_samp_factor + ix;
            int block_idx = block_y * c.width_in_blocks + block_x;
            const coeff_t* coeffs = &c.coeffs[block_idx << 6];
            EncodeDCTBlockSequential(coeffs, dc_huff_table[i], ac_huff_table[i],
                                     &last_dc_coeff[i], &bw,
                                     split_merge_opts, noncrit_bits_ptr);
          }
        }
      }
    }
    if (bw.pos > (1 << 16)) {
      if (!JPEGWrite(out, bw.data.get(), bw.pos)) return false;
      bw.pos = 0;
    }
  }
  bw.JumpToByteBoundary();
  if (!JPEGWrite(out, bw.data.get(), bw.pos)) return false;
  if (split_merge_opts && split_merge_opts->split_jpeg) {
    FILE* f = fopen(split_merge_opts->noncrit_path.c_str(), "wb");
    if (!f) {
      fprintf(stderr, "Failed to open noncrit file for writing: %s\n",
              split_merge_opts->noncrit_path.c_str());
      return false;
    }
    noncrit_writer.WriteToFile(f);
    fclose(f);
  }
  return !bw.overflow;
}

std::vector<uint8_t> ReadFileToVec(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    size_t sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> data(sz);
    size_t bytes_read = fread(data.data(), 1, sz, f);
    if (bytes_read != sz) {
        data.resize(bytes_read);
    }
    fclose(f);
    return data;
}

}  // namespace (anonymous)

// -- Start of functions with external linkage --

void WriteACBitsToNoncrit(const coeff_t* coeffs, SimpleBitWriter* writer) {
  // Write only the actual coefficient value bits for non-zero coefficients
  // The Huffman-coded (run-length, size) tuples stay in the critical file
  for (int k = 1; k < 64; ++k) {
    coeff_t val = coeffs[kJPEGNaturalOrder[k]];
    if (val == 0) {
      continue; // Skip zeros - they're handled by run-length encoding in crit file
    }
    
    coeff_t temp = val;
    coeff_t temp2;

    if (temp < 0) {
      temp = -temp;
      temp2 = ~temp;
    } else {
      temp2 = temp;
    }
    
    int nbits = Log2FloorNonZero(temp) + 1;
    // Only write the actual coefficient value bits (no nbits)
    if (nbits > 0) {
        writer->WriteBits(temp2 & ((1 << nbits) - 1), nbits);
    }
  }
}

void ReadACBitsFromNoncrit(coeff_t* coeffs, SimpleBitReader* reader, const std::vector<int>& ac_sizes) {
  int size_index = 0;
  for (int k = 1; k < 64; ++k) {
    if (size_index < ac_sizes.size()) {
      int nbits = ac_sizes[size_index++];
      if (nbits > 0) {
        int val = reader->ReadBits(nbits);
        if (val < (1 << (nbits - 1))) {
          val -= (1 << nbits) - 1;
        }
        coeffs[kJPEGNaturalOrder[k]] = val;
      } else {
        coeffs[kJPEGNaturalOrder[k]] = 0;
      }
    } else {
      coeffs[kJPEGNaturalOrder[k]] = 0;
    }
  }
}

size_t HistogramHeaderCost(const JpegHistogram& histo) {
  size_t header_bits = 17 * 8;
  for (int i = 0; i + 1 < JpegHistogram::kSize; ++i) {
    if (histo.counts[i] > 0) {
      header_bits += 8;
    }
  }
  return header_bits;
}

size_t HistogramEntropyCost(const JpegHistogram& histo,
                            const uint8_t depths[256]) {
  size_t bits = 0;
  for (int i = 0; i + 1 < JpegHistogram::kSize; ++i) {
    bits += (histo.counts[i] / 2) * (depths[i] + (i & 0xf));
  }
  bits += (bits * 3 + 512) >> 10;
  return bits;
}

void BuildDCHistograms(const JPEGData& jpg, JpegHistogram* histo) {
  for (size_t i = 0; i < jpg.components.size(); ++i) {
    const JPEGComponent& c = jpg.components[i];
    JpegHistogram* dc_histogram = &histo[i];
    coeff_t last_dc_coeff = 0;
    for (int mcu_y = 0; mcu_y < jpg.MCU_rows; ++mcu_y) {
      for (int mcu_x = 0; mcu_x < jpg.MCU_cols; ++mcu_x) {
        for (int iy = 0; iy < c.v_samp_factor; ++iy) {
          for (int ix = 0; ix < c.h_samp_factor; ++ix) {
            int block_y = mcu_y * c.v_samp_factor + iy;
            int block_x = mcu_x * c.h_samp_factor + ix;
            int block_idx = block_y * c.width_in_blocks + block_x;
            coeff_t dc_coeff = c.coeffs[block_idx << 6];
            int diff = std::abs(dc_coeff - last_dc_coeff);
            int nbits = Log2Floor(diff) + 1;
            dc_histogram->Add(nbits);
            last_dc_coeff = dc_coeff;
          }
        }
      }
    }
  }
}

void UpdateACHistogramForDCTBlock(const coeff_t* coeffs,
                                  JpegHistogram* ac_histogram) {
  int r = 0;
  for (int k = 1; k < 64; ++k) {
    coeff_t coeff = coeffs[kJPEGNaturalOrder[k]];
    if (coeff == 0) {
      r++;
      continue;
    }
    while (r > 15) {
      ac_histogram->Add(0xf0);
      r -= 16;
    }
    int nbits = Log2FloorNonZero(std::abs(coeff)) + 1;
    int symbol = (r << 4) + nbits;
    ac_histogram->Add(symbol);
    r = 0;
  }
  if (r > 0) {
    ac_histogram->Add(0);
  }
}

void BuildACHistograms(const JPEGData& jpg, JpegHistogram* histo) {
  for (size_t i = 0; i < jpg.components.size(); ++i) {
    const JPEGComponent& c = jpg.components[i];
    JpegHistogram* ac_histogram = &histo[i];
    for (size_t j = 0; j < c.coeffs.size(); j += kDCTBlockSize) {
      UpdateACHistogramForDCTBlock(&c.coeffs[j], ac_histogram);
    }
  }
}

size_t JpegHeaderSize(const JPEGData& jpg, bool strip_metadata) {
  size_t num_bytes = 0;
  num_bytes += 2;  // SOI
  if (strip_metadata) {
    num_bytes += 18;  // APP0
  } else {
    for (size_t i = 0; i < jpg.app_data.size(); ++i) {
      num_bytes += 1 + jpg.app_data[i].size();
    }
    for (size_t i = 0; i < jpg.com_data.size(); ++i) {
      num_bytes += 2 + jpg.com_data[i].size();
    }
  }
  num_bytes += 4; // DQT
  for (size_t i = 0; i < jpg.quant.size(); ++i) {
    num_bytes += 1 + (jpg.quant[i].precision ? 2 : 1) * kDCTBlockSize;
  }
  num_bytes += 10 + 3 * jpg.components.size();  // SOF
  num_bytes += 4;  // DHT
  num_bytes += 8 + 2 * jpg.components.size();  // SOS
  num_bytes += 2;  // EOI
  num_bytes += jpg.tail_data.size();
  return num_bytes;
}

size_t ClusterHistograms(JpegHistogram* histo, size_t* num,
                         int* histo_indexes, uint8_t* depth) {
  memset(depth, 0, *num * JpegHistogram::kSize);
  size_t costs[kMaxComponents];
  for (size_t i = 0; i < *num; ++i) {
    histo_indexes[i] = i;
    std::vector<HuffmanTree> tree(2 * JpegHistogram::kSize + 1);
    CreateHuffmanTree(histo[i].counts, JpegHistogram::kSize,
                      kJpegHuffmanMaxBitLength, &tree[0],
                      &depth[i * JpegHistogram::kSize]);
    costs[i] = (HistogramHeaderCost(histo[i]) +
                HistogramEntropyCost(histo[i],
                                     &depth[i * JpegHistogram::kSize]));
  }
  const size_t orig_num = *num;
  while (*num > 1) {
    size_t last = *num - 1;
    size_t second_last = *num - 2;
    JpegHistogram combined(histo[last]);
    combined.AddHistogram(histo[second_last]);
    std::vector<HuffmanTree> tree(2 * JpegHistogram::kSize + 1);
    uint8_t depth_combined[JpegHistogram::kSize] = { 0 };
    CreateHuffmanTree(combined.counts, JpegHistogram::kSize,
                      kJpegHuffmanMaxBitLength, &tree[0], depth_combined);
    size_t cost_combined = (HistogramHeaderCost(combined) +
                            HistogramEntropyCost(combined, depth_combined));
    if (cost_combined < costs[last] + costs[second_last]) {
      histo[second_last] = combined;
      histo[last] = JpegHistogram();
      costs[second_last] = cost_combined;
      memcpy(&depth[second_last * JpegHistogram::kSize], depth_combined,
             sizeof(depth_combined));
      for (size_t i = 0; i < orig_num; ++i) {
        if (histo_indexes[i] == last) {
          histo_indexes[i] = second_last;
        }
      }
      --(*num);
    } else {
      break;
    }
  }
  size_t total_cost = 0;
  for (size_t i = 0; i < *num; ++i) {
    total_cost += costs[i];
  }
  return (total_cost + 7) / 8;
}

// Helper function to write AC coefficient sizes to a custom APP marker
bool WriteACSizesMarker(const JPEGData& jpg, JPEGOutput out) {
  // Collect all AC coefficient sizes from the original data
  std::vector<uint8_t> sizes;
  for (auto& comp : jpg.components) {
    for (size_t i = 0; i < comp.coeffs.size(); i += kDCTBlockSize) {
      for (int k = 1; k < 64; ++k) {
        coeff_t val = comp.coeffs[i + kJPEGNaturalOrder[k]];
        if (val != 0) {
          int nbits = Log2FloorNonZero(std::abs(val)) + 1;
          sizes.push_back(nbits);
        } else {
          sizes.push_back(0);
        }
      }
    }
  }
  
  fprintf(stderr, "Writing AC coefficient sizes marker with %zu sizes\n", sizes.size());
  
  // Write custom APP1 marker with AC coefficient sizes
  std::vector<uint8_t> marker_data;
  marker_data.push_back('A');
  marker_data.push_back('C');
  marker_data.push_back('S');
  marker_data.push_back('I');
  marker_data.insert(marker_data.end(), sizes.begin(), sizes.end());
  
  // Write APP1 marker
  uint8_t app1_header[4] = { 0xff, 0xe1, 
                              static_cast<uint8_t>((marker_data.size() + 2) >> 8),
                              static_cast<uint8_t>((marker_data.size() + 2) & 0xff) };
  
  fprintf(stderr, "APP1 marker size: %d bytes\n", (marker_data.size() + 2));
  
  return (JPEGWrite(out, app1_header, sizeof(app1_header)) &&
          JPEGWrite(out, marker_data.data(), marker_data.size()));
}

bool WriteJpeg(const JPEGData& jpg, bool strip_metadata, JPEGOutput out,
               const SplitMergeOptions* split_merge_opts) {
  static const uint8_t kSOIMarker[2] = { 0xff, 0xd8 };
  static const uint8_t kEOIMarker[2] = { 0xff, 0xd9 };
  std::vector<HuffmanCodeTable> dc_codes;
  std::vector<HuffmanCodeTable> ac_codes;
  
  bool result = JPEGWrite(out, kSOIMarker, sizeof(kSOIMarker));
  
  return (result &&
          EncodeMetadata(jpg, strip_metadata, out) &&
          // If splitting, write AC coefficient sizes marker after existing metadata
          (!split_merge_opts || !split_merge_opts->split_jpeg || 
           (fprintf(stderr, "Splitting JPEG - writing AC coefficient sizes marker\n"), 
            WriteACSizesMarker(jpg, out))) &&
          EncodeDQT(jpg.quant, out) &&
          EncodeSOF(jpg, out) &&
          BuildAndEncodeHuffmanCodes(jpg, out, &dc_codes, &ac_codes) &&
          EncodeScan(jpg, dc_codes, ac_codes, out, split_merge_opts) &&
          JPEGWrite(out, kEOIMarker, sizeof(kEOIMarker)) &&
          (strip_metadata || JPEGWrite(out, jpg.tail_data)));
}

// Helper function to extract AC coefficient sizes from the critical file
std::vector<int> ExtractACSizesFromHuffmanData(const JPEGData& jpg, const std::string& crit_data) {
  std::vector<int> ac_sizes;
  
  // Look for a custom APP marker that contains the AC coefficient sizes
  // This would be added during the split process
  fprintf(stderr, "Searching for AC coefficient sizes marker in %zu bytes\n", crit_data.size());
  
  for (size_t i = 0; i < crit_data.size() - 2; ++i) {
    if (crit_data[i] == 0xFF && crit_data[i + 1] == 0xE1) {
      fprintf(stderr, "Found APP1 marker at position %zu\n", i);
      // Found APP1 marker - check if it's our custom marker
      if (i + 4 < crit_data.size()) {
        int marker_len = (crit_data[i + 2] << 8) | crit_data[i + 3];
        fprintf(stderr, "APP1 marker length: %d bytes\n", marker_len);
        if (i + 4 + marker_len <= crit_data.size()) {
          // Check if this is our custom marker
          std::string marker_data(crit_data.begin() + i + 4, 
                                 crit_data.begin() + i + 4 + marker_len);
          fprintf(stderr, "Marker data starts with: %c%c%c%c\n", 
                  marker_data[0], marker_data[1], marker_data[2], marker_data[3]);
          if (marker_data.substr(0, 4) == "ACSI") {
            fprintf(stderr, "Found our custom AC coefficient sizes marker!\n");
            // Found our custom marker with AC coefficient sizes
            // Parse the sizes from the marker data
            size_t pos = 4;
            while (pos < marker_data.size()) {
              if (pos + 1 < marker_data.size()) {
                int size = marker_data[pos];
                ac_sizes.push_back(size);
                pos++;
              } else {
                break;
              }
            }
            fprintf(stderr, "Extracted %zu AC coefficient sizes\n", ac_sizes.size());
            return ac_sizes;
          }
        }
      }
    }
  }
  
  // If no custom marker found, use default sizes
  fprintf(stderr, "No AC coefficient sizes marker found, using defaults\n");
  for (auto& comp : jpg.components) {
    for (size_t i = 0; i < comp.coeffs.size(); i += kDCTBlockSize) {
      for (int k = 1; k < 64; ++k) {
        int nbits = 0;
        if (k < 10) {
          nbits = 4; // Low frequency coefficients
        } else if (k < 30) {
          nbits = 3; // Mid frequency coefficients  
        } else {
          nbits = 2; // High frequency coefficients
        }
        ac_sizes.push_back(nbits);
      }
    }
  }
  
  return ac_sizes;
}

bool MergeCritNoncrit(const std::string& crit_path,
                      const std::string& noncrit_path,
                      const std::string& out_path) {
  // **CRASH FIX**: Store the vector in a named variable to prevent its
  // immediate destruction and the resulting dangling pointer.
  std::vector<uint8_t> crit_vec = ReadFileToVec(crit_path);
  if (crit_vec.empty()) {
      fprintf(stderr, "Failed to read critical file: %s\n", crit_path.c_str());
      return false;
  }
  std::string crit_data_str(reinterpret_cast<const char*>(crit_vec.data()), crit_vec.size());

  JPEGData jpg;
  if (!ReadJpeg(crit_data_str, JPEG_READ_ALL, &jpg)) {
    fprintf(stderr, "Failed to parse critical JPEG data from %s\n", crit_path.c_str());
    return false;
  }

  std::vector<uint8_t> noncrit_vec = ReadFileToVec(noncrit_path);
  if (noncrit_vec.empty()) {
      fprintf(stderr, "Failed to read non-critical file: %s\n", noncrit_path.c_str());
      return false;
  }
  SimpleBitReader reader(noncrit_vec.data(), noncrit_vec.size());

  // Extract AC coefficient sizes from the critical file's Huffman-coded data
  std::vector<int> ac_sizes = ExtractACSizesFromHuffmanData(jpg, crit_data_str);

  for (auto& comp : jpg.components) {
    for (size_t i = 0; i < comp.coeffs.size(); i += kDCTBlockSize) {
      ReadACBitsFromNoncrit(&comp.coeffs[i], &reader, ac_sizes);
    }
  }

  std::string out_data;
  JPEGOutput output(GuetzliStringOut, &out_data);
  if (!WriteJpeg(jpg, false, output, nullptr)) {
    fprintf(stderr, "Failed to write merged JPEG data.\n");
    return false;
  }

  FILE* fout = fopen(out_path.c_str(), "wb");
  if (!fout) {
    fprintf(stderr, "Failed to open output file for writing: %s\n", out_path.c_str());
    return false;
  }
  fwrite(out_data.data(), 1, out_data.size(), fout);
  fclose(fout);

  return true;
}

}  // namespace guetzli
