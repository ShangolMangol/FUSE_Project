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

// Functions for writing a JPEGData object into a jpeg byte stream.

#ifndef GUETZLI_JPEG_DATA_WRITER_H_
#define GUETZLI_JPEG_DATA_WRITER_H_

#include <stdint.h>
#include <string.h>
#include <vector>
#include <string>

#include "guetzli/jpeg_data.h"

namespace guetzli {

// Function pointer type used to write len bytes into buf. Returns the
// number of bytes written or -1 on error.
typedef int (*JPEGOutputHook)(void* data, const uint8_t* buf, size_t len);

// Output callback function with associated data.
struct JPEGOutput {
  JPEGOutput(JPEGOutputHook cb, void* data) : cb(cb), data(data) {}
  bool Write(const uint8_t* buf, size_t len) const {
    return (len == 0) || (cb(data, buf, len) == len);
  }
 private:
  JPEGOutputHook cb;
  void* data;
};

// New struct for split/merge options
struct SplitMergeOptions {
  bool split_jpeg = false;
  bool merge_jpeg = false;
  std::string crit_path;
  std::string noncrit_path;
  std::string merge_crit_path;
  std::string merge_noncrit_path;
};

// Updated WriteJpeg signature
bool WriteJpeg(const JPEGData& jpg, bool strip_metadata, JPEGOutput out,
               const SplitMergeOptions* split_merge_opts = nullptr);

// Declare merge function for split JPEGs
bool MergeCritNoncrit(const std::string& crit_path, const std::string& noncrit_path, const std::string& out_path);

struct HuffmanCodeTable {
  uint8_t depth[256];
  int code[256];
};

void BuildSequentialHuffmanCodes(
    const JPEGData& jpg, std::vector<HuffmanCodeTable>* dc_huffman_code_tables,
    std::vector<HuffmanCodeTable>* ac_huffman_code_tables);

struct JpegHistogram {
  static const int kSize = kJpegHuffmanAlphabetSize + 1;

  JpegHistogram() { Clear(); }
  void Clear() {
    memset(counts, 0, sizeof(counts));
    counts[kSize - 1] = 1;
  }
  void Add(int symbol) {
    counts[symbol] += 2;
  }
  void Add(int symbol, int weight) {
    counts[symbol] += 2 * weight;
  }
  void AddHistogram(const JpegHistogram& other) {
    for (int i = 0; i + 1 < kSize; ++i) {
      counts[i] += other.counts[i];
    }
    counts[kSize - 1] = 1;
  }
  int NumSymbols() const {
    int n = 0;
    for (int i = 0; i + 1 < kSize; ++i) {
      n += (counts[i] > 0 ? 1 : 0);
    }
    return n;
  }

  uint32_t counts[kSize];
};

void BuildDCHistograms(const JPEGData& jpg, JpegHistogram* histo);
void BuildACHistograms(const JPEGData& jpg, JpegHistogram* histo);
size_t JpegHeaderSize(const JPEGData& jpg, bool strip_metadata);
size_t EstimateJpegDataSize(const int num_components,
                            const std::vector<JpegHistogram>& histograms);

size_t HistogramEntropyCost(const JpegHistogram& histo,
                            const uint8_t depths[256]);
size_t HistogramHeaderCost(const JpegHistogram& histo);

void UpdateACHistogramForDCTBlock(const coeff_t* coeffs,
                                  JpegHistogram* ac_histogram);

size_t ClusterHistograms(JpegHistogram* histo, size_t* num, int* histo_indexes,
                         uint8_t* depths);

// Bit-level writer for .noncrit file
struct SimpleBitWriter {
  std::vector<uint8_t> data;
  uint8_t cur_byte = 0;
  int bit_pos = 0; // 0-7
  void WriteBits(uint32_t bits, int nbits) {
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
  void Flush() {
    if (bit_pos > 0) {
      cur_byte <<= (8 - bit_pos);
      data.push_back(cur_byte);
      cur_byte = 0;
      bit_pos = 0;
    }
  }
  void WriteToFile(FILE* f) {
    Flush();
    fwrite(data.data(), 1, data.size(), f);
  }
};

// Bit-level reader for .noncrit file
struct SimpleBitReader {
  const uint8_t* data;
  size_t size;
  size_t byte_pos = 0;
  int bit_pos = 0; // 0-7
  SimpleBitReader(const uint8_t* d, size_t s) : data(d), size(s) {}
  uint32_t ReadBits(int nbits) {
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
};

// Update signature to accept void* for noncrit_bits
void EncodeDCTBlockSequential(const coeff_t* coeffs,
                              const HuffmanCodeTable& dc_huff,
                              const HuffmanCodeTable& ac_huff,
                              coeff_t* last_dc_coeff,
                              BitWriter* bw,
                              const SplitMergeOptions* split_merge_opts,
                              void* noncrit_bits);

}  // namespace guetzli

#endif  // GUETZLI_JPEG_DATA_WRITER_H_
