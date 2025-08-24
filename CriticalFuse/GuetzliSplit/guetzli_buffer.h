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

#ifndef GUETZLI_BUFFER_H_
#define GUETZLI_BUFFER_H_

#include <string>
#include <vector>

namespace guetzli {

// Buffer-based split function that takes JPEG data as input and returns critical and non-critical parts
// The files are saved to disk with the naming convention: base_path.jpg.crit and base_path.jpg.ac.noncrit
bool SplitJpegBuffer(const std::string& jpeg_data, 
                     std::string& critical_data, 
                     std::string& noncritical_data,
                     const std::string& base_path);

// Buffer-based merge function that takes critical and non-critical parts and returns complete JPEG
// The files are read from disk with the naming convention: base_path.jpg.crit and base_path.jpg.ac.noncrit
bool MergeJpegBuffer(const std::string& critical_data, 
                     const std::string& noncritical_data, 
                     std::string& jpeg_data,
                     const std::string& base_path);

}  // namespace guetzli

#endif  // GUETZLI_BUFFER_H_
