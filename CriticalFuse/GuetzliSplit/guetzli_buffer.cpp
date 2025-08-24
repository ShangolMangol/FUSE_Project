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
        return ".jpeg";
    } else if (base_path.length() >= 4 && base_path.substr(base_path.length() - 4) == ".jpg") {
        return ".jpg";
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
    // Determine the correct extension
    std::string ext = GetJpegExtension(base_path);

    // Use the existing file paths for the stored files
    std::string crit_file_path = base_path + ext + ".crit";
    std::string noncrit_file_path = base_path + ext + ".noncrit";
    std::string temp_output_file = "temp_output.jpg";

    // Write critical data to the stored file location
    std::ofstream crit_file(crit_file_path, std::ios::binary);
    if (!crit_file.is_open()) {
        std::cerr << "Failed to open critical file for writing: " << crit_file_path << std::endl;
        return false;
    }
    crit_file.write(critical_data.data(), critical_data.size());
    crit_file.close();

    // Write non-critical data to the stored file location
    std::ofstream noncrit_file(noncrit_file_path, std::ios::binary);
    if (!noncrit_file.is_open()) {
        std::cerr << "Failed to open non-critical file for writing: " << noncrit_file_path << std::endl;
        return false;
    }
    noncrit_file.write(noncritical_data.data(), noncritical_data.size());
    noncrit_file.close();

    // Use the existing MergeCritNoncrit function
    bool success = MergeCritNoncrit(crit_file_path, noncrit_file_path, temp_output_file);

    if (success) {
        // Read the merged output
        std::ifstream output_file(temp_output_file, std::ios::binary);
        if (output_file.is_open()) {
            std::stringstream buffer;
            buffer << output_file.rdbuf();
            jpeg_data = buffer.str();
            output_file.close();
        } else {
            success = false;
        }
    }

    // Clean up only the temporary output file, not the stored files
    std::remove(temp_output_file.c_str());

    return success;
}

}  // namespace guetzli
