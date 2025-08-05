#include "guetzli/jpeg_data_reader.h"
#include "guetzli/jpeg_data_writer.h"
#include <iostream>
#include <fstream>
#include <string>

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

  // Read headers file
  std::ifstream headers_file(headers_path, std::ios::binary);
  if (!headers_file) {
    std::cerr << "Failed to open headers file: " << headers_path << std::endl;
    return false;
  }
  std::string headers_data((std::istreambuf_iterator<char>(headers_file)),
                           std::istreambuf_iterator<char>());
  headers_file.close();

  // Read the other split files
  std::ifstream rlesize_file(rlesize_path, std::ios::binary);
  if (!rlesize_file) {
    std::cerr << "Failed to open RLE+size file: " << rlesize_path << std::endl;
    return false;
  }
  std::vector<uint8_t> rlesize_data((std::istreambuf_iterator<char>(rlesize_file)),
                                     std::istreambuf_iterator<char>());
  rlesize_file.close();

  std::ifstream dc_file(dc_path, std::ios::binary);
  if (!dc_file) {
    std::cerr << "Failed to open DC file: " << dc_path << std::endl;
    return false;
  }
  std::vector<uint8_t> dc_data((std::istreambuf_iterator<char>(dc_file)),
                                std::istreambuf_iterator<char>());
  dc_file.close();

  std::ifstream ac_file(ac_path, std::ios::binary);
  if (!ac_file) {
    std::cerr << "Failed to open AC file: " << ac_path << std::endl;
    return false;
  }
  std::vector<uint8_t> ac_data((std::istreambuf_iterator<char>(ac_file)),
                                std::istreambuf_iterator<char>());
  ac_file.close();

  // Parse the headers to get JPEG structure
  guetzli::JPEGData jpg;
  if (!guetzli::ReadJpeg(headers_data, guetzli::JPEG_READ_ALL, &jpg)) {
    std::cerr << "Failed to parse headers file" << std::endl;
    return false;
  }

  // Create readers for the split data
  guetzli::SimpleBitReader rlesize_reader(rlesize_data.data(), rlesize_data.size());
  guetzli::SimpleBitReader dc_reader(dc_data.data(), dc_data.size());
  guetzli::SimpleBitReader ac_reader(ac_data.data(), ac_data.size());

  // Reconstruct the coefficients from the split data
  // The split process writes raw bits and RLE+size information from Huffman decoding
  
  guetzli::coeff_t last_dc_coeff[3] = {0}; // Assuming max 3 components
  
  for (size_t comp_idx = 0; comp_idx < jpg.components.size(); comp_idx++) {
    auto& comp = jpg.components[comp_idx];
    
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
         int dc_diff = dc_raw_bits;
         if (dc_diff < (1 << (dc_huffman_symbol - 1))) {
           dc_diff -= (1 << dc_huffman_symbol) - 1;
         }
         block_coeffs[0] = last_dc_coeff[comp_idx] + dc_diff;
         last_dc_coeff[comp_idx] = block_coeffs[0];
       } else {
         block_coeffs[0] = last_dc_coeff[comp_idx];
       }

             // Read AC coefficients using Huffman symbol data
       int k = 1; // Start from first AC coefficient
       while (k < 64) {
         int ac_huffman_symbol = rlesize_reader.ReadBits(8); // Read AC Huffman symbol (RLE+size)
         int rle = ac_huffman_symbol >> 4;
         int size = ac_huffman_symbol & 15;
        
        // Skip zeros based on RLE
        k += rle;
        
        if (k >= 64) break; // End of block
        
        if (size > 0) {
          int ac_raw_bits = ac_reader.ReadBits(size); // Read size bits for AC coefficient
          // Reconstruct AC coefficient using raw bits
          int ac_val = ac_raw_bits;
          if (ac_val < (1 << (size - 1))) {
            ac_val -= (1 << size) - 1;
          }
          block_coeffs[k] = static_cast<guetzli::coeff_t>(ac_val);
        } else {
          block_coeffs[k] = 0;
        }
        k++;
      }
      
      // Fill remaining coefficients with zeros
      while (k < 64) {
        block_coeffs[k] = 0;
        k++;
      }
    }
  }

  // Write the reconstructed JPEG
  std::string output_data;
  guetzli::JPEGOutput output(StringOut, &output_data);
  
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

  std::cout << "Successfully merged files into: " << output_path << std::endl;
  return true;
}

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <split|merge> <input_file>" << std::endl;
    std::cerr << "  split: Split a JPEG file into separate components" << std::endl;
    std::cerr << "  merge: Merge split files back into a complete JPEG" << std::endl;
    return 1;
  }

  std::string mode = argv[1];
  std::string input_file = argv[2];

  if (mode == "merge") {
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
    std::cerr << "Invalid mode. Use 'split' or 'merge'" << std::endl;
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