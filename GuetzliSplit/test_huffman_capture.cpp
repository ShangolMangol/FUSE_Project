#include "guetzli/jpeg_data_reader.h"
#include "guetzli/jpeg_data_writer.h"
#include <iostream>
#include <fstream>
#include <string>

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <input_jpeg_file>" << std::endl;
    return 1;
  }

  std::string input_file = argv[1];
  
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

  // Write headers file (JPEG headers without scan data)
  std::string headers_data;
  guetzli::JPEGOutput headers_output(guetzli::GuetzliStringOut, &headers_data);
  
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