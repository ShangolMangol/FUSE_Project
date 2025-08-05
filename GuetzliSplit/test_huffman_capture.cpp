#include "guetzli/jpeg_data_reader.h"
#include "guetzli/jpeg_data_writer.h"
#include "guetzli/jpeg_error.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

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
  
  // Debug: Print first few bytes of headers file
  std::cout << "Headers file first 16 bytes: ";
  for (int i = 0; i < std::min(16, (int)headers_data.size()); ++i) {
    printf("%02x ", (unsigned char)headers_data[i]);
  }
  std::cout << std::endl;
  
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

  // Reconstruct the coefficients from the split data
  // The split process writes raw bits and RLE+size information from Huffman decoding
  
  guetzli::coeff_t last_dc_coeff[3] = {0}; // Assuming max 3 components
  
  std::cout << "Reconstructing coefficients..." << std::endl;
  
  for (size_t comp_idx = 0; comp_idx < jpg.components.size(); comp_idx++) {
    auto& comp = jpg.components[comp_idx];
    std::cout << "Processing component " << comp_idx << " with " << comp.coeffs.size() << " coefficients" << std::endl;
    
    for (size_t i = 0; i < comp.coeffs.size(); i += 64) { // 64 coefficients per block
      guetzli::coeff_t* block_coeffs = &comp.coeffs[i];
      
      // Read DC coefficient data - the split process writes Huffman symbol and raw bits
      int dc_huffman_symbol = dc_reader.ReadBits(8); // Read DC Huffman symbol (size category)
      int dc_raw_bits = 0;
      if (dc_huffman_symbol > 0) {
        dc_raw_bits = dc_reader.ReadBits(dc_huffman_symbol); // Read exactly SIZE bits for the raw bits
      }
      
      if (dc_huffman_symbol > 0) {
        // Reconstruct DC coefficient using the raw bits
        // Apply HuffExtend logic: (x < (1 << (s - 1)) ? x - (1 << s) + 1 : x)
        int dc_diff = dc_raw_bits;
        if (dc_diff < (1 << (dc_huffman_symbol - 1))) {
          dc_diff -= (1 << dc_huffman_symbol) - 1;
        }
        block_coeffs[0] = last_dc_coeff[comp_idx] + dc_diff;
        last_dc_coeff[comp_idx] = block_coeffs[0];
      } else {
        block_coeffs[0] = last_dc_coeff[comp_idx];
      }

      // Read AC coefficients using Huffman symbol data from rlesize file
      int k = 1; // Start from first AC coefficient
      int eobrun = 0; // Track end-of-block runs
      
      while (k < 64) {
        // Handle EOB runs
        if (eobrun > 0) {
          eobrun--;
          break; // End of block
        }
        
        int ac_huffman_symbol = rlesize_reader.ReadBits(8); // Read AC Huffman symbol (RLE+size)
        int rle = ac_huffman_symbol >> 4;
        int size = ac_huffman_symbol & 15;
        
        // Debug: Print symbol information
        if (size == 0 && rle != 15) {
          std::cout << "  EOB symbol: RLE=" << rle << ", Size=" << size << std::endl;
        }
        
        if (size > 0) {
          // Skip zeros based on RLE
          k += rle;
          
          if (k >= 64) break; // End of block
          
          int ac_raw_bits = ac_reader.ReadBits(size); // Read size bits for AC coefficient
          // Reconstruct AC coefficient using raw bits
          // Apply HuffExtend logic: (x < (1 << (s - 1)) ? x - (1 << s) + 1 : x)
          int ac_val = ac_raw_bits;
          if (ac_val < (1 << (size - 1))) {
            ac_val -= (1 << size) - 1;
          }
          // Apply SignedLeftshift: (v >= 0) ? (v << Al) : -((-v) << Al)
          // Note: Al is 0 for baseline JPEG, so this is just a cast
          block_coeffs[k] = static_cast<guetzli::coeff_t>(ac_val);
          k++;
        } else if (rle == 15) {
          // 16 zeros, skip 15 and continue to next coefficient
          k += 15;
        } else {
          // End of block reached - set up EOB run
          eobrun = 1 << rle;
          if (rle > 0) {
            // Read additional EOB run bits from rlesize file (they were captured there)
            int additional_bits = rlesize_reader.ReadBits(rle);
            eobrun += additional_bits;
          }
          break;
        }
      }
      
      // Fill remaining coefficients with zeros
      while (k < 64) {
        block_coeffs[k] = 0;
        k++;
      }
    }
  }

  std::cout << "Writing reconstructed JPEG..." << std::endl;

  // Write the reconstructed JPEG
  std::string output_data;
  guetzli::JPEGOutput output(StringOut, &output_data);
  
  // Clear any error state that might have been set during reconstruction
  jpg.error = guetzli::JPEG_OK;
  
  if (!guetzli::WriteJpeg(jpg, false, output, nullptr)) {
    std::cerr << "Failed to write merged JPEG" << std::endl;
    return false;
  }

  // Write the output file
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
  
  // First, split the file
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
  
  // Read the input JPEG file
  std::ifstream file(input_file, std::ios::binary);
  if (!file) {
    std::cerr << "Failed to open input file: " << input_file << std::endl;
    return false;
  }
  
  std::string jpeg_data((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
  file.close();

  // Create writers for capturing Huffman-decoded data
  guetzli::SimpleBitWriter rlesize_writer;
  guetzli::SimpleBitWriter dc_raw_writer;
  guetzli::SimpleBitWriter ac_raw_writer;

  // Read JPEG with capture
  guetzli::JPEGData jpg;
  if (!guetzli::ReadJpegWithCapture(jpeg_data, guetzli::JPEG_READ_ALL, &jpg, &rlesize_writer, &dc_raw_writer, &ac_raw_writer)) {
    std::cerr << "Failed to read JPEG file with capture" << std::endl;
    return false;
  }

  // Write headers file (JPEG headers without scan data)
  std::string headers_data;
  guetzli::JPEGOutput headers_output(StringOut, &headers_data);
  
  // Create a copy of the JPEG data with zeroed coefficients for headers
  guetzli::JPEGData headers_jpg = jpg;
  for (auto& comp : headers_jpg.components) {
    for (size_t i = 0; i < comp.coeffs.size(); ++i) {
      comp.coeffs[i] = 0;
    }
  }
  
  if (!guetzli::WriteJpeg(headers_jpg, false, headers_output, nullptr)) {
    std::cerr << "Failed to write headers file" << std::endl;
    return false;
  }

  // Debug: Print first few bytes of headers data
  std::cout << "Headers data first 16 bytes: ";
  for (int i = 0; i < std::min(16, (int)headers_data.size()); ++i) {
    printf("%02x ", (unsigned char)headers_data[i]);
  }
  std::cout << std::endl;
  std::cout << "Headers data size: " << headers_data.size() << " bytes" << std::endl;

  // Write the split files
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

  // Now merge the files back
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

  // The original ReadJpegWithCapture wrote the Huffman-decoded data
  // but we need to ensure we have the complete information for reconstruction
  // The capture process already wrote the raw bits and RLE+size information

  // Write headers file (JPEG headers without scan data)
  std::string headers_data;
  guetzli::JPEGOutput headers_output(StringOut, &headers_data);
  
  // Create a copy of the JPEG data with zeroed coefficients for headers
  guetzli::JPEGData headers_jpg = jpg;
  for (auto& comp : headers_jpg.components) {
    for (size_t i = 0; i < comp.coeffs.size(); ++i) {
      comp.coeffs[i] = 0;
    }
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