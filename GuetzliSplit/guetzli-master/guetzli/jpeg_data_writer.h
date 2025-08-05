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

#ifndef GUETZLI_JPEG_DATA_WRITER_H_
#define GUETZLI_JPEG_DATA_WRITER_H_

#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>

#include "guetzli/jpeg_data.h"

namespace guetzli {

struct BitWriter;

// Full definition of HuffmanCodeTable, required by any file that uses it.
struct HuffmanCodeTable {
  uint8_t depth[256];
  int code[256];
};

// New struct for split/merge options
struct SplitMergeOptions {
  bool split_jpeg = false;
  bool merge_jpeg = false;
  std::string crit_path;
  std::string noncrit_path;
  std::string nbits_path;
  std::string merge_crit_path;
  std::string merge_noncrit_path;
  std::string merge_nbits_path;
};

// Function pointer type used to write len bytes into buf.
typedef int (*JPEGOutputHook)(void* data, const uint8_t* buf, size_t len);

// Output callback function with associated data.
struct JPEGOutput {
  JPEGOutput(JPEGOutputHook cb, void* data) : cb_(cb), data_(data) {}
  bool Write(const uint8_t* buf, size_t len) const {
    return (len == 0) || (static_cast<size_t>(cb_(data_, buf, len)) == len);
  }
 private:
  JPEGOutputHook cb_;
  void* data_;
};

// Main function to write JPEG data, now with split/merge capabilities.
bool WriteJpeg(const JPEGData& jpg, bool strip_metadata, JPEGOutput out,
               const SplitMergeOptions* split_merge_opts);

// Main function to merge split JPEG files.
bool MergeCritNoncrit(const std::string& crit_path,
                      const std::string& noncrit_path,
                      const std::string& out_path);

// Bit-level writer for .noncrit file
struct SimpleBitWriter {
  std::vector<uint8_t> data;
  uint8_t cur_byte = 0;
  int bit_pos = 0; // 0-7

  void WriteBits(uint32_t bits, int nbits);
  void Flush();
  void WriteToFile(FILE* f);
};

// Bit-level reader for .noncrit file
struct SimpleBitReader {
  const uint8_t* data;
  size_t size;
  size_t byte_pos = 0;
  int bit_pos = 0; // 0-7

  SimpleBitReader(const uint8_t* d, size_t s) : data(d), size(s) {}
  uint32_t ReadBits(int nbits);
};


// The following are helper functions used by both jpeg_data_writer.cc and
// processor.cc. They must be declared here to be visible across files.

struct JpegHistogram {
  static const int kSize = kJpegHuffmanAlphabetSize + 1;

  JpegHistogram();
  void Clear();
  void Add(int symbol);
  void Add(int symbol, int weight);
  void AddHistogram(const JpegHistogram& other);
  int NumSymbols() const;

  uint32_t counts[kSize];
};

// Functions for building and analyzing histograms, needed by processor.cc
void BuildDCHistograms(const JPEGData& jpg, JpegHistogram* histo);
void BuildACHistograms(const JPEGData& jpg, JpegHistogram* histo);
void UpdateACHistogramForDCTBlock(const coeff_t* coeffs,
                                  JpegHistogram* ac_histogram);
size_t JpegHeaderSize(const JPEGData& jpg, bool strip_metadata);
size_t HistogramEntropyCost(const JpegHistogram& histo, const uint8_t depths[256]);
size_t HistogramHeaderCost(const JpegHistogram& histo);
size_t ClusterHistograms(JpegHistogram* histo, size_t* num, int* histo_indexes,
                         uint8_t* depths);

// Functions for split/merge logic
void WriteACBitsToNoncrit(const coeff_t* coeffs, SimpleBitWriter* writer);
void ReadACBitsFromNoncrit(coeff_t* coeffs, SimpleBitReader* reader);

// New functions for separated nbits and values
void WriteACNbitsToFile(const coeff_t* coeffs, SimpleBitWriter* writer);
void WriteACValuesToFile(const coeff_t* coeffs, SimpleBitWriter* writer);
void ReadACNbitsFromFile(coeff_t* coeffs, SimpleBitReader* reader);
void ReadACValuesFromFile(coeff_t* coeffs, SimpleBitReader* reader);

// Structures for capturing Huffman-decoded data
struct DecodedDCData {
  int huffman_symbol;  // Huffman-decoded symbol
  int raw_bits;        // Raw bits read after Huffman decoding
  int nbits;           // Number of bits
};

struct DecodedACData {
  int huffman_symbol;  // Huffman-decoded symbol (RLE+size)
  int raw_bits;        // Raw bits read after Huffman decoding
  int rle;             // Run length (extracted from symbol)
  int size;            // Size bits (extracted from symbol)
};

// Functions for capturing Huffman-decoded data during JPEG reading
void CaptureDecodedDCData(const DecodedDCData& data, SimpleBitWriter* writer);
void CaptureDecodedACData(const DecodedACData& data, SimpleBitWriter* writer);
void ReadDecodedDCData(DecodedDCData* data, SimpleBitReader* reader);
void ReadDecodedACData(DecodedACData* data, SimpleBitReader* reader);

// Simplified functions for storing Huffman encoded (RLE,size) and raw bits
void WriteHuffmanRleSize(int rle, int size, SimpleBitWriter* writer);
void WriteRawBits(int bits, int nbits, SimpleBitWriter* writer);
void ReadHuffmanRleSize(int* rle, int* size, SimpleBitReader* reader);
void ReadRawBits(int* bits, int nbits, SimpleBitReader* reader);

}  // namespace guetzli

#endif  // GUETZLI_JPEG_DATA_WRITER_H_
