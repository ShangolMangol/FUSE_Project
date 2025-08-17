// --- START OF FILE guetzli.cc ---

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

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <sstream>
#include <string.h>
#include "png.h"
#include "guetzli/jpeg_data.h"
#include "guetzli/jpeg_data_reader.h"
#include "guetzli/jpeg_data_writer.h"
#include "guetzli/processor.h"
#include "guetzli/quality.h"
#include "guetzli/stats.h"

namespace {

constexpr int kDefaultJPEGQuality = 95;
constexpr int kBytesPerPixel = 350;
constexpr int kLowestMemusageMB = 100; // in MB
constexpr int kDefaultMemlimitMB = 6000; // in MB

inline uint8_t BlendOnBlack(const uint8_t val, const uint8_t alpha) {
  return (static_cast<int>(val) * static_cast<int>(alpha) + 128) / 255;
}

bool ReadPNG(const std::string& data, int* xsize, int* ysize,
             std::vector<uint8_t>* rgb) {
  png_structp png_ptr =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png_ptr) {
    return false;
  }
  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr) {
    png_destroy_read_struct(&png_ptr, nullptr, nullptr);
    return false;
  }
  if (setjmp(png_jmpbuf(png_ptr)) != 0) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return false;
  }
  std::istringstream memstream(data, std::ios::in | std::ios::binary);
  png_set_read_fn(png_ptr, static_cast<void*>(&memstream), [](png_structp png_ptr, png_bytep outBytes, png_size_t byteCountToRead) {
    std::istringstream& memstream = *static_cast<std::istringstream*>(png_get_io_ptr(png_ptr));
    memstream.read(reinterpret_cast<char*>(outBytes), byteCountToRead);
    if (memstream.eof()) png_error(png_ptr, "unexpected end of data");
    if (memstream.fail()) png_error(png_ptr, "read from memory error");
  });
  const unsigned int png_transforms =
      PNG_TRANSFORM_PACKING | PNG_TRANSFORM_EXPAND | PNG_TRANSFORM_STRIP_16;
  png_read_png(png_ptr, info_ptr, png_transforms, nullptr);
  png_bytep* row_pointers = png_get_rows(png_ptr, info_ptr);
  *xsize = png_get_image_width(png_ptr, info_ptr);
  *ysize = png_get_image_height(png_ptr, info_ptr);
  rgb->resize(3 * (*xsize) * (*ysize));
  const int components = png_get_channels(png_ptr, info_ptr);
  switch (components) {
    case 1: {
      for (int y = 0; y < *ysize; ++y) {
        const uint8_t* row_in = row_pointers[y];
        uint8_t* row_out = &(*rgb)[3 * y * (*xsize)];
        for (int x = 0; x < *xsize; ++x) {
          row_out[3 * x + 0] = row_in[x];
          row_out[3 * x + 1] = row_in[x];
          row_out[3 * x + 2] = row_in[x];
        }
      }
      break;
    }
    case 2: {
      for (int y = 0; y < *ysize; ++y) {
        const uint8_t* row_in = row_pointers[y];
        uint8_t* row_out = &(*rgb)[3 * y * (*xsize)];
        for (int x = 0; x < *xsize; ++x) {
          const uint8_t gray = BlendOnBlack(row_in[2 * x], row_in[2 * x + 1]);
          row_out[3 * x + 0] = gray;
          row_out[3 * x + 1] = gray;
          row_out[3 * x + 2] = gray;
        }
      }
      break;
    }
    case 3: {
      for (int y = 0; y < *ysize; ++y) {
        memcpy(&(*rgb)[3 * y * (*xsize)], row_pointers[y], 3 * (*xsize));
      }
      break;
    }
    case 4: {
      for (int y = 0; y < *ysize; ++y) {
        const uint8_t* row_in = row_pointers[y];
        uint8_t* row_out = &(*rgb)[3 * y * (*xsize)];
        for (int x = 0; x < *xsize; ++x) {
          const uint8_t alpha = row_in[4 * x + 3];
          row_out[3 * x + 0] = BlendOnBlack(row_in[4 * x + 0], alpha);
          row_out[3 * x + 1] = BlendOnBlack(row_in[4 * x + 1], alpha);
          row_out[3 * x + 2] = BlendOnBlack(row_in[4 * x + 2], alpha);
        }
      }
      break;
    }
    default:
      png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
      return false;
  }
  png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
  return true;
}

std::string ReadFileOrDie(const char* filename) {
  bool read_from_stdin = strncmp(filename, "-", 2) == 0;
  FILE* f = read_from_stdin ? stdin : fopen(filename, "rb");
  if (!f) {
    fprintf(stderr, "Can't open input file: %s\n", filename);
    perror("Details");
    exit(1);
  }
  std::string result;
  off_t buffer_size = 8192;
  if (fseek(f, 0, SEEK_END) == 0) {
    buffer_size = std::max<off_t>(ftell(f), 1);
    fseek(f, 0, SEEK_SET);
  }
  std::unique_ptr<char[]> buf(new char[buffer_size]);
  while (!feof(f)) {
    size_t read_bytes = fread(buf.get(), 1, buffer_size, f);
    if (ferror(f)) {
      perror("fread");
      exit(1);
    }
    result.append(buf.get(), read_bytes);
  }
  fclose(f);
  return result;
}

void WriteFileOrDie(const char* filename, const std::string& contents) {
  bool write_to_stdout = strncmp(filename, "-", 2) == 0;
  FILE* f = write_to_stdout ? stdout : fopen(filename, "wb");
  if (!f) {
    perror("Can't open output file for writing");
    exit(1);
  }
  if (fwrite(contents.data(), 1, contents.size(), f) != contents.size()) {
    perror("fwrite");
    exit(1);
  }
  if (fclose(f) < 0) {
    perror("fclose");
    exit(1);
  }
}

void TerminateHandler() {
  fprintf(stderr, "Unhandled exception. Most likely insufficient memory available.\n"
          "Make sure that there is 300MB/MPix of memory available.\n");
  exit(1);
}

void Usage() {
  fprintf(stderr,
      "Guetzli JPEG compressor. Usage: \n"
      "guetzli [flags] input_filename output_filename\n"
      "\n"
      "Flags:\n"
      "  --verbose      - Print a verbose trace of all attempts.\n"
      "  --quality Q    - Visual quality to aim for (JPEG quality value). Default: %d\n"
      "  --memlimit M   - Memory limit in MB. Default: %d\n"
      "  --nomemlimit   - Do not limit memory usage.\n"
      "  --split        - Output a .jpg.crit and .jpg.noncrit file instead of a JPEG.\n"
      "                   The output_filename is used as a base name.\n"
      "  --merge        - Input is a .jpg.crit file, merges with corresponding\n"
      "                   .jpg.noncrit file to produce a JPEG.\n",
      kDefaultJPEGQuality, kDefaultMemlimitMB);
  exit(1);
}

}  // namespace

int main(int argc, char** argv) {
  std::set_terminate(TerminateHandler);

  int verbose = 0;
  int quality = kDefaultJPEGQuality;
  int memlimit_mb = kDefaultMemlimitMB;
  bool split_mode = false;
  bool merge_mode = false;

  int opt_idx = 1;
  for (; opt_idx < argc; ++opt_idx) {
    if (strnlen(argv[opt_idx], 2) < 2 || argv[opt_idx][0] != '-') break;
    if (!strcmp(argv[opt_idx], "--verbose")) {
      verbose = 1;
    } else if (!strcmp(argv[opt_idx], "--quality")) {
      if (++opt_idx >= argc) Usage();
      quality = atoi(argv[opt_idx]);
    } else if (!strcmp(argv[opt_idx], "--memlimit")) {
      if (++opt_idx >= argc) Usage();
      memlimit_mb = atoi(argv[opt_idx]);
    } else if (!strcmp(argv[opt_idx], "--nomemlimit")) {
      memlimit_mb = -1;
    } else if (!strcmp(argv[opt_idx], "--split")) {
      split_mode = true;
    } else if (!strcmp(argv[opt_idx], "--merge")) {
      merge_mode = true;
    } else if (!strcmp(argv[opt_idx], "--")) {
      ++opt_idx;
      break;
    } else {
      fprintf(stderr, "Unknown commandline flag: %s\n", argv[opt_idx]);
      Usage();
    }
  }

  if (argc - opt_idx != 2) {
    Usage();
  }

  if (split_mode && merge_mode) {
    fprintf(stderr, "Cannot use --split and --merge at the same time.\n");
    Usage();
  }

  const char* in_filename = argv[opt_idx];
  const char* out_filename = argv[opt_idx + 1];

  guetzli::SplitMergeOptions split_opts;
  split_opts.split_jpeg = split_mode;
  split_opts.merge_jpeg = merge_mode;

  if (merge_mode) {
    std::string base_path = in_filename;
    size_t crit_pos = base_path.rfind(".jpg.crit");
    if (crit_pos != std::string::npos) {
      base_path = base_path.substr(0, crit_pos);
    } else {
      // Fallback for old .crit format
      size_t old_crit_pos = base_path.rfind(".crit");
      if (old_crit_pos != std::string::npos) {
        base_path = base_path.substr(0, old_crit_pos);
      }
    }
    split_opts.merge_crit_path = base_path + ".jpg.crit";
    split_opts.merge_noncrit_path = base_path + ".jpg.ac.noncrit";

    fprintf(stderr, "Merging %s and %s into %s\n",
            split_opts.merge_crit_path.c_str(),
            split_opts.merge_noncrit_path.c_str(),
            out_filename);

    if (!guetzli::MergeCritNoncrit(split_opts.merge_crit_path,
                                  split_opts.merge_noncrit_path,
                                  out_filename)) {
      fprintf(stderr, "Merge failed.\n");
      return 1;
    }
    fprintf(stderr, "Merge successful.\n");
    return 0;
  }

  if (split_mode) {
    fprintf(stderr, "DEBUG: Setting up split mode paths\n");
    std::string base_path = out_filename;
    fprintf(stderr, "DEBUG: Original out_filename: %s\n", out_filename);
    size_t jpg_pos = base_path.rfind(".jpg");
    if (jpg_pos != std::string::npos) {
      base_path = base_path.substr(0, jpg_pos);
      fprintf(stderr, "DEBUG: Found .jpg extension, base_path now: %s\n", base_path.c_str());
    }
    size_t jpeg_pos = base_path.rfind(".jpeg");
    if (jpeg_pos != std::string::npos) {
        base_path = base_path.substr(0, jpeg_pos);
        fprintf(stderr, "DEBUG: Found .jpeg extension, base_path now: %s\n", base_path.c_str());
    }
    split_opts.crit_path = base_path + ".jpg.crit";
    split_opts.noncrit_path = base_path + ".jpg.ac.noncrit";
    fprintf(stderr, "DEBUG: crit_path: %s\n", split_opts.crit_path.c_str());
    fprintf(stderr, "DEBUG: noncrit_path: %s\n", split_opts.noncrit_path.c_str());
  }

  fprintf(stderr, "DEBUG: Reading input file: %s\n", in_filename);
  std::string in_data = ReadFileOrDie(in_filename);
  fprintf(stderr, "DEBUG: Input file size: %zu bytes\n", in_data.size());
  std::string out_data;

  guetzli::Params params;
  params.butteraugli_target =
      static_cast<float>(guetzli::ButteraugliScoreForQuality(quality));
  // Note: params.split_jpeg is not used in the current implementation
  // The split logic is controlled by the split_merge_opts parameter
  fprintf(stderr, "DEBUG: split_mode = %d\n", split_mode);

  guetzli::ProcessStats stats;
  if (verbose) {
    stats.debug_output_file = stderr;
  }

  static const unsigned char kPNGMagicBytes[] = {
      0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n',
  };
  if (in_data.size() >= 8 &&
      memcmp(in_data.data(), kPNGMagicBytes, sizeof(kPNGMagicBytes)) == 0) {
    int xsize, ysize;
    std::vector<uint8_t> rgb;
    if (!ReadPNG(in_data, &xsize, &ysize, &rgb)) {
      fprintf(stderr, "Error reading PNG data from input file\n");
      return 1;
    }
    double pixels = static_cast<double>(xsize) * ysize;
    if (memlimit_mb != -1 && (pixels * kBytesPerPixel / (1 << 20) > memlimit_mb ||
                                memlimit_mb < kLowestMemusageMB)) {
      fprintf(stderr, "Memory limit would be exceeded. Failing.\n");
      return 1;
    }
    if (!guetzli::Process(params, &stats, rgb, xsize, ysize, &out_data, split_mode ? &split_opts : nullptr)) {
      fprintf(stderr, "Guetzli processing failed\n");
      return 1;
    }
  } else {
    fprintf(stderr, "DEBUG: Processing as JPEG\n");
    guetzli::JPEGData jpg_header;
    if (!guetzli::ReadJpeg(in_data, guetzli::JPEG_READ_HEADER, &jpg_header)) {
      fprintf(stderr, "Input is not a PNG and not a valid JPEG\n");
      return 1;
    }
    fprintf(stderr, "DEBUG: JPEG header read successfully, dimensions: %dx%d\n", jpg_header.width, jpg_header.height);
    double pixels = static_cast<double>(jpg_header.width) * jpg_header.height;
    if (memlimit_mb != -1 && (pixels * kBytesPerPixel / (1 << 20) > memlimit_mb ||
                                memlimit_mb < kLowestMemusageMB)) {
      fprintf(stderr, "Memory limit would be exceeded. Failing.\n");
      return 1;
    }
    fprintf(stderr, "DEBUG: Calling guetzli::Process with split_opts\n");
    if (!guetzli::Process(params, &stats, in_data, &out_data, split_mode ? &split_opts : nullptr)) {
      fprintf(stderr, "Guetzli processing failed\n");
      return 1;
    }
    fprintf(stderr, "DEBUG: guetzli::Process completed successfully\n");
  }

  if (split_mode) {
    // The .crit file is returned in out_data, and the .ac.noncrit file is written during Process.
    fprintf(stderr, "Writing %s\n", split_opts.crit_path.c_str());
    WriteFileOrDie(split_opts.crit_path.c_str(), out_data);
    fprintf(stderr, "Wrote %s\n", split_opts.noncrit_path.c_str());
  } else {
    WriteFileOrDie(out_filename, out_data);
  }

  return 0;
}
// --- END OF FILE guetzli.cc ---
