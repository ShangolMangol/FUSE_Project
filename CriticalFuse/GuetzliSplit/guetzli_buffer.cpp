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

#include "guetzli_buffer.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>

// Include the necessary Guetzli headers
#include "jpeg_data.h"
#include "jpeg_data_reader.h"
#include "jpeg_data_writer.h"
#include "processor.h"
#include "quality.h"
#include "stats.h"

// Helper function to read file to vector (if not already defined)
std::vector<uint8_t> LoadFileToBuffer(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return std::vector<uint8_t>();
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    return data;
}

namespace guetzli {

// This callback is used when merging to write to a std::string.
static int GuetzliStringOut(void* data, const uint8_t* buf, size_t len) {
  std::string* out = reinterpret_cast<std::string*>(data);
  out->append(reinterpret_cast<const char*>(buf), len);
  return len; // Return number of bytes written
}

// Helper function to determine the correct extension for file naming
std::string GetJpegExtension(const std::string& base_path) {
    // Check if the original file had .jpeg or .jpg extension
    if (base_path.length() >= 5 && base_path.substr(base_path.length() - 5) == ".jpeg") {
        return ""; // Extension already present
    } else if (base_path.length() >= 4 && base_path.substr(base_path.length() - 4) == ".jpg") {
        return ""; // Extension already present
    }
    // Default to .jpg if no extension found
    return ".jpg";
}

bool SplitJpegBuffer(const std::string& jpeg_data, 
                     std::string& critical_data, 
                     std::string& noncritical_data,
                     const std::string& base_path) {
    // Parse the JPEG data
    JPEGData jpg;
    if (!ReadJpeg(jpeg_data, JPEG_READ_ALL, &jpg)) {
        std::cerr << "Failed to parse JPEG data" << std::endl;
        return false;
    }

    // Determine the correct extension
    std::string ext = GetJpegExtension(base_path);

    // Set up split options with proper file paths
    SplitMergeOptions split_opts;
    split_opts.split_jpeg = true;
    split_opts.crit_path = base_path + ext + ".crit";
    split_opts.noncrit_path = base_path + ext + ".noncrit";

    // Set up parameters for processing
    Params params;
    params.butteraugli_target = static_cast<float>(ButteraugliScoreForQuality(95)); // Default quality

    // Process the JPEG data to split it
    std::string out_data;
    ProcessStats stats;
    
    if (!Process(params, &stats, jpeg_data, &out_data, &split_opts)) {
        std::cerr << "Failed to process JPEG for splitting" << std::endl;
        return false;
    }

    // The critical data is returned in out_data
    critical_data = out_data;

    // Read the non-critical data from the file that was written by Guetzli
    std::ifstream noncrit_file(split_opts.noncrit_path, std::ios::binary);
    if (noncrit_file.is_open()) {
        std::stringstream buffer;
        buffer << noncrit_file.rdbuf();
        noncritical_data = buffer.str();
        noncrit_file.close();
        // Note: We don't remove the noncrit file - it's the stored file
    } else {
        std::cerr << "Failed to read non-critical file: " << split_opts.noncrit_path << std::endl;
        return false;
    }

    return true;
}

bool MergeJpegBuffer(const std::string& critical_data, 
                     const std::string& noncritical_data, 
                     std::string& jpeg_data,
                     const std::string& base_path) {
    // Write critical data to a temporary file for merging
    std::string crit_file_path = base_path + ".temp_crit";
    std::ofstream crit_file(crit_file_path, std::ios::binary);
    if (!crit_file.is_open()) {
        std::cerr << "Failed to open critical file for writing: " << crit_file_path << std::endl;
        return false;
    }
    crit_file.write(critical_data.data(), critical_data.size());
    crit_file.close();

    // Write non-critical data to a temporary file for merging
    std::string noncrit_file_path = base_path + ".temp_noncrit";
    std::ofstream noncrit_file(noncrit_file_path, std::ios::binary);
    if (!noncrit_file.is_open()) {
        std::cerr << "Failed to open non-critical file for writing: " << noncrit_file_path << std::endl;
        std::remove(crit_file_path.c_str());
        return false;
    }
    noncrit_file.write(noncritical_data.data(), noncritical_data.size());
    noncrit_file.close();

    // Merge directly to memory using a string buffer
    std::string merged_data;
    JPEGOutput output(GuetzliStringOut, &merged_data);
    
    // Read the critical file and merge with non-critical data
    std::vector<uint8_t> crit_vec = LoadFileToBuffer(crit_file_path);
    std::vector<uint8_t> ac_vec = LoadFileToBuffer(noncrit_file_path);
    
    if (crit_vec.empty() || ac_vec.empty()) {
        std::cerr << "Failed to read temporary files for merging" << std::endl;
        std::remove(crit_file_path.c_str());
        std::remove(noncrit_file_path.c_str());
        return false;
    }

    JPEGData jpg;
    bool read_success = ReadJpeg(reinterpret_cast<const uint8_t*>(crit_vec.data()), 
                               crit_vec.size(),
                               ac_vec.data(), ac_vec.size(),
                               JPEG_READ_ALL, &jpg);
    
    if (!read_success) {
        std::cerr << "Failed to parse critical JPEG data" << std::endl;
        std::remove(crit_file_path.c_str());
        std::remove(noncrit_file_path.c_str());
        return false;
    }

    // Write the complete merged JPEG directly to memory
    if (!WriteJpeg(jpg, false, output, nullptr)) {
        std::cerr << "Failed to write merged JPEG data to memory" << std::endl;
        std::remove(crit_file_path.c_str());
        std::remove(noncrit_file_path.c_str());
        return false;
    }

    // Copy the merged data to the output parameter
    jpeg_data = merged_data;

    // Clean up temporary files
    std::remove(crit_file_path.c_str());
    std::remove(noncrit_file_path.c_str());

    return true;
}

}  // namespace guetzli
