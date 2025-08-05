#include "guetzli/jpeg_data_reader.h"
#include "guetzli/jpeg_data_writer.h"
#include "guetzli/jpeg_error.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm> // For std::remove

// String output function for JPEGOutput
int StringOut(void* data, const uint8_t* buf, size_t len) {
  std::string* out = reinterpret_cast<std::string*>(data);
  out->append(reinterpret_cast<const char*>(buf), len);
  return len;
}

// Function to merge split files back into a complete JPEG
bool MergeSplitFiles(const std::string& base_name) {
  std::string headers_path = base_name + ".jpg.headers";
  std::string rlesize_path = base_name + ".jpg.rlesize";
  std::string dc_path = base_name + ".jpg.dc";
  std::string ac_path = base_name + ".jpg.ac";
  std::string output_path = base_name + "_merged.jpg";

  std::cout << "Merging files with base name: " << base_name << std::endl;
  std::cout << "Headers file: " << headers_path << std::endl;
  std::cout << "RLE+Size file: " << rlesize_path << std::endl;
  std::cout << "DC file: " << dc_path << std::endl;
  std::cout << "AC file: " << ac_path << std::endl;

  // Read headers file
  std::ifstream headers_file(headers_path, std::ios::binary);
  if (!headers_file) {
    std::cerr << "Failed to open headers file: " << headers_path << std::endl;
    return false;
  }
  std::string headers_data((std::istreambuf_iterator<char>(headers_file)),
                           std::istreambuf_iterator<char>());
  headers_file.close();
  std::cout << "Read headers file: " << headers_data.size() << " bytes" << std::endl;

  // Read the other split files
  std::ifstream rlesize_file(rlesize_path, std::ios::binary);
  if (!rlesize_file) {
    std::cerr << "Failed to open RLE+size file: " << rlesize_path << std::endl;
    return false;
  }
  std::vector<uint8_t> rlesize_data((std::istreambuf_iterator<char>(rlesize_file)),
                                     std::istreambuf_iterator<char>());
  rlesize_file.close();
  std::cout << "Read RLE+Size file: " << rlesize_data.size() << " bytes" << std::endl;

  std::ifstream dc_file(dc_path, std::ios::binary);
  if (!dc_file) {
    std::cerr << "Failed to open DC file: " << dc_path << std::endl;
    return false;
  }
  std::vector<uint8_t> dc_data((std::istreambuf_iterator<char>(dc_file)),
                                std::istreambuf_iterator<char>());
  dc_file.close();
  std::cout << "Read DC file: " << dc_data.size() << " bytes" << std::endl;

  std::ifstream ac_file(ac_path, std::ios::binary);
  if (!ac_file) {
    std::cerr << "Failed to open AC file: " << ac_path << std::endl;
    return false;
  }
  std::vector<uint8_t> ac_data((std::istreambuf_iterator<char>(ac_file)),
                                std::istreambuf_iterator<char>());
  ac_file.close();
  std::cout << "Read AC file: " << ac_data.size() << " bytes" << std::endl;

  // Parse the headers to get JPEG structure
  guetzli::JPEGData jpg;
  
  std::cout << "Headers file first 16 bytes: ";
  for (int i = 0; i < std::min(16, (int)headers_data.size()); ++i) {
    printf("%02x ", (unsigned char)headers_data[i]);
  }
  std::cout << std::endl;
  
  // Read with JPEG_READ_ALL. Because our new headers file has no scan data,
  // this will parse all metadata and correctly allocate the coefficient buffers.
  if (!guetzli::ReadJpeg(headers_data, guetzli::JPEG_READ_ALL, &jpg)) {
    std::cerr << "Failed to parse headers file" << std::endl;
    std::cerr << "Headers file size: " << headers_data.size() << " bytes" << std::endl;
    return false;
  }
  std::cout << "Parsed JPEG headers successfully" << std::endl;
  std::cout << "Image dimensions: " << jpg.width << "x" << jpg.height << std::endl;
  std::cout << "Number of components: " << jpg.components.size() << std::endl;

  // Create readers for the split data
  guetzli::SimpleBitReader rlesize_reader(rlesize_data.data(), rlesize_data.size());
  guetzli::SimpleBitReader dc_reader(dc_data.data(), dc_data.size());
  guetzli::SimpleBitReader ac_reader(ac_data.data(), ac_data.size());

  guetzli::coeff_t last_dc_coeff[3] = {0};
  
  std::cout << "Reconstructing coefficients..." << std::endl;
  
  for (size_t comp_idx = 0; comp_idx < jpg.components.size(); comp_idx++) {
    auto& comp = jpg.components[comp_idx];
    
    for (size_t i = 0; i < comp.coeffs.size(); i += 64) {
      guetzli::coeff_t* block_coeffs = &comp.coeffs[i];
      
      int dc_huffman_symbol = dc_reader.ReadBits(8);
      int dc_raw_bits = 0;
      if (dc_huffman_symbol > 0) {
        dc_raw_bits = dc_reader.ReadBits(dc_huffman_symbol);
      }
      
      int dc_diff = 0;
      if (dc_huffman_symbol > 0) {
        dc_diff = dc_raw_bits;
        if (dc_diff < (1 << (dc_huffman_symbol - 1))) {
          dc_diff -= (1 << dc_huffman_symbol) - 1;
        }
      }
      block_coeffs[0] = last_dc_coeff[comp_idx] + dc_diff;
      last_dc_coeff[comp_idx] = block_coeffs[0];

      // Zero out AC coefficients before filling them
      for (int k = 1; k < 64; ++k) {
        block_coeffs[k] = 0;
      }

      int k = 1;
      // Correctly handle eobrun. It is not a boolean flag.
      int eobrun = 0;
      while (k < 64) {
        if (eobrun > 0) {
            eobrun--;
            // This EOB run applies to the rest of the MCU blocks for this component.
            // We break here to start the next 8x8 block.
            break;
        }

        int ac_huffman_symbol = rlesize_reader.ReadBits(8);
        int rle = ac_huffman_symbol >> 4;
        int size = ac_huffman_symbol & 15;
        
        if (size > 0) {
          k += rle;
          if (k >= 64) break;
          
          int ac_raw_bits = ac_reader.ReadBits(size);
          int ac_val = ac_raw_bits;
          if (ac_val < (1 << (size - 1))) {
            ac_val -= (1 << size) - 1;
          }
          block_coeffs[guetzli::kJPEGNaturalOrder[k]] = static_cast<guetzli::coeff_t>(ac_val);
          k++;
        } else if (rle == 15) { // ZRL (16 zeros)
          k += 16;
        } else { // EOB
          eobrun = 1 << rle;
          if (rle > 0) {
            int additional_bits = rlesize_reader.ReadBits(rle);
            eobrun += additional_bits;
          }
          // Decrement for the current block and break
          eobrun--;
          break;
        }
      }
    }
  }

  std::cout << "Writing reconstructed JPEG..." << std::endl;

  std::string output_data;
  guetzli::JPEGOutput output(StringOut, &output_data);
  
  jpg.error = guetzli::JPEG_OK;
  
  if (!guetzli::WriteJpeg(jpg, false, output, nullptr)) {
    std::cerr << "Failed to write merged JPEG" << std::endl;
    return false;
  }

  std::ofstream output_file(output_path, std::ios::binary);
  if (!output_file) {
    std::cerr << "Failed to open output file for writing: " << output_path << std::endl;
    return false;
  }
  output_file.write(output_data.data(), output_data.size());
  output_file.close();

  std::cout << "Successfully merged files into: " << output_path << " (" << output_data.size() << " bytes)" << std::endl;
  return true;
}

// Function to test the split and merge process
bool TestSplitAndMerge(const std::string& input_file) {
  std::cout << "Testing split and merge with file: " << input_file << std::endl;
  
  std::string base_name = input_file;
  size_t dot_pos = base_name.find_last_of('.');
  if (dot_pos != std::string::npos) {
    base_name = base_name.substr(0, dot_pos);
  }
  
  std::string headers_path = base_name + ".jpg.headers";
  std::string rlesize_path = base_name + ".jpg.rlesize";
  std::string dc_path = base_name + ".jpg.dc";
  std::string ac_path = base_name + ".jpg.ac";
  std::string merged_path = base_name + "_merged.jpg";
  
  std::ifstream file(input_file, std::ios::binary);
  if (!file) {
    std::cerr << "Failed to open input file: " << input_file << std::endl;
    return false;
  }
  
  std::string jpeg_data((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
  file.close();

  guetzli::SimpleBitWriter rlesize_writer;
  guetzli::SimpleBitWriter dc_raw_writer;
  guetzli::SimpleBitWriter ac_raw_writer;

  guetzli::JPEGData jpg;
  if (!guetzli::ReadJpegWithCapture(jpeg_data, guetzli::JPEG_READ_ALL, &jpg, &rlesize_writer, &dc_raw_writer, &ac_raw_writer)) {
    std::cerr << "Failed to read JPEG file with capture" << std::endl;
    return false;
  }

  // Create a header-only JPEGData object by removing scan-related info.
  std::string headers_data;
  guetzli::JPEGOutput headers_output(StringOut, &headers_data);
  
  guetzli::JPEGData headers_jpg = jpg;
  // Clear all scan-related information. This prevents WriteJpeg from writing a scan.
  headers_jpg.scan_info.clear();
  headers_jpg.marker_order.erase(
      std::remove(headers_jpg.marker_order.begin(), headers_jpg.marker_order.end(), 0xda), // remove SOS
      headers_jpg.marker_order.end());
  // Let WriteJpeg add its own EOI
  headers_jpg.marker_order.erase(
      std::remove(headers_jpg.marker_order.begin(), headers_jpg.marker_order.end(), 0xd9), // remove EOI
      headers_jpg.marker_order.end());
  
  // Coefficients are not needed for the header file.
  for (auto& comp : headers_jpg.components) {
    comp.coeffs.clear();
  }
  
  if (!guetzli::WriteJpeg(headers_jpg, false, headers_output, nullptr)) {
    std::cerr << "Failed to write headers file" << std::endl;
    return false;
  }

  std::cout << "Headers data first 16 bytes: ";
  for (int i = 0; i < std::min(16, (int)headers_data.size()); ++i) {
    printf("%02x ", (unsigned char)headers_data[i]);
  }
  std::cout << std::endl;
  std::cout << "Headers data size: " << headers_data.size() << " bytes" << std::endl;

  FILE* headers_f = fopen(headers_path.c_str(), "wb");
  if (!headers_f) {
    std::cerr << "Failed to open headers file for writing: " << headers_path << std::endl;
    return false;
  }
  fwrite(headers_data.data(), 1, headers_data.size(), headers_f);
  fclose(headers_f);

  FILE* rlesize_f = fopen(rlesize_path.c_str(), "wb");
  if (!rlesize_f) {
    std::cerr << "Failed to open RLE+size file for writing: " << rlesize_path << std::endl;
    return false;
  }
  rlesize_writer.WriteToFile(rlesize_f);
  fclose(rlesize_f);

  FILE* dc_f = fopen(dc_path.c_str(), "wb");
  if (!dc_f) {
    std::cerr << "Failed to open DC file for writing: " << dc_path << std::endl;
    return false;
  }
  dc_raw_writer.WriteToFile(dc_f);
  fclose(dc_f);

  FILE* ac_f = fopen(ac_path.c_str(), "wb");
  if (!ac_f) {
    std::cerr << "Failed to open AC file for writing: " << ac_path << std::endl;
    return false;
  }
  ac_raw_writer.WriteToFile(ac_f);
  fclose(ac_f);

  std::cout << "Successfully split JPEG into:" << std::endl;
  std::cout << "  Headers: " << headers_path << std::endl;
  std::cout << "  RLE+Size: " << rlesize_path << std::endl;
  std::cout << "  DC raw bits: " << dc_path << std::endl;
  std::cout << "  AC raw bits: " << ac_path << std::endl;

  if (!MergeSplitFiles(base_name)) {
    std::cerr << "Failed to merge split files" << std::endl;
    return false;
  }

  std::cout << "Successfully merged files into: " << merged_path << std::endl;
  
  // Compare original and merged files
  std::ifstream original_file(input_file, std::ios::binary);
  std::ifstream merged_file(merged_path, std::ios::binary);
  
  if (!original_file || !merged_file) {
    std::cerr << "Failed to open files for comparison" << std::endl;
    return false;
  }
  
  std::string original_data((std::istreambuf_iterator<char>(original_file)),
                            std::istreambuf_iterator<char>());
  std::string merged_data((std::istreambuf_iterator<char>(merged_file)),
                          std::istreambuf_iterator<char>());
  
  original_file.close();
  merged_file.close();
  
  if (original_data == merged_data) {
    std::cout << "SUCCESS: Original and merged files are identical!" << std::endl;
    return true;
  } else {
    std::cout << "WARNING: Original and merged files differ!" << std::endl;
    std::cout << "Original size: " << original_data.size() << " bytes" << std::endl;
    std::cout << "Merged size: " << merged_data.size() << " bytes" << std::endl;
    return false;
  }
}

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <split|merge|test> <input_file>" << std::endl;
    std::cerr << "  split: Split a JPEG file into separate components" << std::endl;
    std::cerr << "  merge: Merge split files back into a complete JPEG" << std::endl;
    std::cerr << "  test:  Test the complete split and merge process" << std::endl;
    return 1;
  }

  std::string mode = argv[1];
  std::string input_file = argv[2];

  if (mode == "test") {
    if (!TestSplitAndMerge(input_file)) {
      return 1;
    }
    return 0;
  } else if (mode == "merge") {
    // Extract base name for merge
    std::string base_name = input_file;
    size_t dot_pos = base_name.find_last_of('.');
    if (dot_pos != std::string::npos) {
      base_name = base_name.substr(0, dot_pos);
    }
    
    if (!MergeSplitFiles(base_name)) {
      return 1;
    }
    return 0;
  } else if (mode != "split") {
    std::cerr << "Invalid mode. Use 'split', 'merge', or 'test'" << std::endl;
    return 1;
  }

  // Original split functionality
  // Read the input JPEG file
  std::ifstream file(input_file, std::ios::binary);
  if (!file) {
    std::cerr << "Failed to open input file: " << input_file << std::endl;
    return 1;
  }
  
  std::string jpeg_data((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
  file.close();

  // Create output file paths
  std::string base_name = input_file;
  size_t dot_pos = base_name.find_last_of('.');
  if (dot_pos != std::string::npos) {
    base_name = base_name.substr(0, dot_pos);
  }
  
  std::string headers_path = base_name + ".jpg.headers";
  std::string rlesize_path = base_name + ".jpg.rlesize";
  std::string dc_path = base_name + ".jpg.dc";
  std::string ac_path = base_name + ".jpg.ac";

  // Create writers for capturing Huffman-decoded data
  guetzli::SimpleBitWriter rlesize_writer;
  guetzli::SimpleBitWriter dc_raw_writer;
  guetzli::SimpleBitWriter ac_raw_writer;

  // Read JPEG with capture
  guetzli::JPEGData jpg;
  if (!guetzli::ReadJpegWithCapture(jpeg_data, guetzli::JPEG_READ_ALL, &jpg, &rlesize_writer, &dc_raw_writer, &ac_raw_writer)) {
    std::cerr << "Failed to read JPEG file with capture" << std::endl;
    return 1;
  }

  // Create a header-only JPEGData object by removing scan-related info.
  std::string headers_data;
  guetzli::JPEGOutput headers_output(StringOut, &headers_data);
  
  guetzli::JPEGData headers_jpg = jpg;
  // Clear all scan-related information. This prevents WriteJpeg from writing a scan.
  headers_jpg.scan_info.clear();
  headers_jpg.marker_order.erase(
      std::remove(headers_jpg.marker_order.begin(), headers_jpg.marker_order.end(), 0xda), // remove SOS
      headers_jpg.marker_order.end());
  // Let WriteJpeg add its own EOI
  headers_jpg.marker_order.erase(
      std::remove(headers_jpg.marker_order.begin(), headers_jpg.marker_order.end(), 0xd9), // remove EOI
      headers_jpg.marker_order.end());
  
  // Coefficients are not needed for the header file.
  for (auto& comp : headers_jpg.components) {
    comp.coeffs.clear();
  }
  
  if (!guetzli::WriteJpeg(headers_jpg, false, headers_output, nullptr)) {
    std::cerr << "Failed to write headers file" << std::endl;
    return 1;
  }

  // Debug: Print first few bytes of headers data
  std::cout << "Headers data first 16 bytes: ";
  for (int i = 0; i < std::min(16, (int)headers_data.size()); ++i) {
    printf("%02x ", (unsigned char)headers_data[i]);
  }
  std::cout << std::endl;
  std::cout << "Headers data size: " << headers_data.size() << " bytes" << std::endl;

  // Write the files
  FILE* headers_f = fopen(headers_path.c_str(), "wb");
  if (!headers_f) {
    std::cerr << "Failed to open headers file for writing: " << headers_path << std::endl;
    return 1;
  }
  fwrite(headers_data.data(), 1, headers_data.size(), headers_f);
  fclose(headers_f);

  FILE* rlesize_f = fopen(rlesize_path.c_str(), "wb");
  if (!rlesize_f) {
    std::cerr << "Failed to open RLE+size file for writing: " << rlesize_path << std::endl;
    return 1;
  }
  rlesize_writer.WriteToFile(rlesize_f);
  fclose(rlesize_f);

  FILE* dc_f = fopen(dc_path.c_str(), "wb");
  if (!dc_f) {
    std::cerr << "Failed to open DC file for writing: " << dc_path << std::endl;
    return 1;
  }
  dc_raw_writer.WriteToFile(dc_f);
  fclose(dc_f);

  FILE* ac_f = fopen(ac_path.c_str(), "wb");
  if (!ac_f) {
    std::cerr << "Failed to open AC file for writing: " << ac_path << std::endl;
    return 1;
  }
  ac_raw_writer.WriteToFile(ac_f);
  fclose(ac_f);

  std::cout << "Successfully split JPEG into:" << std::endl;
  std::cout << "  Headers: " << headers_path << std::endl;
  std::cout << "  RLE+Size: " << rlesize_path << std::endl;
  std::cout << "  DC raw bits: " << dc_path << std::endl;
  std::cout << "  AC raw bits: " << ac_path << std::endl;

  return 0;
} 