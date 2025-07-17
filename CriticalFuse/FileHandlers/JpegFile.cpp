// Full C++ class using libjpeg to split JPEG into AC and critical (DC + headers) parts
// Requires libjpeg (e.g., libjpeg-turbo)

#include "JpegFile.h"
#include <jpeglib.h>
#include <vector>
#include <fstream>
#include <iostream>
#include <cstring>
#include <string>

// Helper to copy coefficient arrays safely from decompressor to compressor
static jvirt_barray_ptr* copy_coeff_arrays(jpeg_compress_struct* cinfo, jvirt_barray_ptr* src, jpeg_decompress_struct& dinfo) {
    jvirt_barray_ptr* dst = (jvirt_barray_ptr*)
        (*cinfo->mem->alloc_small)((j_common_ptr)cinfo, JPOOL_IMAGE, sizeof(jvirt_barray_ptr) * dinfo.num_components);

    for (int comp = 0; comp < dinfo.num_components; ++comp) {
        jpeg_component_info* sci = &dinfo.comp_info[comp];
        jpeg_component_info* dci = &cinfo->comp_info[comp];
        JDIMENSION h = sci->height_in_blocks;
        JDIMENSION w = sci->width_in_blocks;

        dst[comp] = cinfo->mem->request_virt_barray((j_common_ptr)cinfo, JPOOL_IMAGE, TRUE, w, h, 1);

        JBLOCKARRAY src_buf = dinfo.mem->access_virt_barray((j_common_ptr)&dinfo, src[comp], 0, h, FALSE);
        JBLOCKARRAY dst_buf = cinfo->mem->access_virt_barray((j_common_ptr)cinfo, dst[comp], 0, h, TRUE);

        for (JDIMENSION row = 0; row < h; ++row) {
            std::memcpy(dst_buf[row], src_buf[row], sizeof(JBLOCK) * w);
        }
    }

    return dst;
}

void JpegFileHandler::splitACCoefficientsExact(const char* jpegData, size_t dataSize) {
    std::cerr << "[DEBUG] splitACCoefficientsExact: called, dataSize=" << dataSize << std::endl;
    criticalData.clear();
    acCoefficientValues.clear();

    // 1. Set up decompression
    jpeg_decompress_struct dinfo;
    jpeg_error_mgr jerr;
    dinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&dinfo);
    jpeg_mem_src(&dinfo, reinterpret_cast<const unsigned char*>(jpegData), dataSize);
    jpeg_read_header(&dinfo, TRUE);

    // Check: Basic Image Info
    std::cerr << "[DEBUG] Image width: " << dinfo.image_width << std::endl;
    std::cerr << "[DEBUG] Image height: " << dinfo.image_height << std::endl;
    std::cerr << "[DEBUG] Number of components: " << dinfo.num_components << std::endl;
    std::cerr << "[DEBUG] Color space: " << dinfo.jpeg_color_space << std::endl;

    // jpeg_read_coefficients - Reads the contents of JPEG file as DCT coefficients
    jvirt_barray_ptr* coeffs_dinfo = jpeg_read_coefficients(&dinfo);
    std::cerr << "[DEBUG] Read coefficients from decompressor" << std::endl;

    // 2. Set up compression
    jpeg_compress_struct cinfo;
    jpeg_error_mgr cerr;
    cinfo.err = jpeg_std_error(&cerr);
    jpeg_create_compress(&cinfo);
    jpeg_copy_critical_parameters(&dinfo, &cinfo);

    // 3. Allocate virtual barray for compressor
    jvirt_barray_ptr* coeffs_cinfo = (jvirt_barray_ptr*)
        (*cinfo.mem->alloc_small)((j_common_ptr)&cinfo, JPOOL_IMAGE, sizeof(jvirt_barray_ptr) * dinfo.num_components);

    for (int comp = 0; comp < dinfo.num_components; ++comp) {
        jpeg_component_info* sci = &dinfo.comp_info[comp];
        JDIMENSION h = sci->height_in_blocks;
        JDIMENSION w = sci->width_in_blocks;
        coeffs_cinfo[comp] = cinfo.mem->request_virt_barray((j_common_ptr)&cinfo, JPOOL_IMAGE, TRUE, w, h, 1);
        std::cerr << "[DEBUG] Allocated coeffs_cinfo[" << comp << "] = " << coeffs_cinfo[comp] << " (w=" << w << ", h=" << h << ")" << std::endl;
    }

    // 4. Copy DC, extract/zero AC
    size_t acCount = 0;
    for (int comp = 0; comp < dinfo.num_components; ++comp) {
        jpeg_component_info* ci = &dinfo.comp_info[comp];
        JDIMENSION h = ci->height_in_blocks;
        JDIMENSION w = ci->width_in_blocks;

        JBLOCKARRAY src_buf = dinfo.mem->access_virt_barray((j_common_ptr)&dinfo, coeffs_dinfo[comp], 0, h, FALSE);
        JBLOCKARRAY dst_buf = cinfo.mem->access_virt_barray((j_common_ptr)&cinfo, coeffs_cinfo[comp], 0, h, TRUE);

        for (JDIMENSION row = 0; row < h; ++row) {
            if (!src_buf[row] || !dst_buf[row]) {
                std::cerr << "[ERROR] Null buffer row at comp " << comp << ", row " << row << std::endl;
                continue;
            }

            for (JDIMENSION col = 0; col < w; ++col) {
                JCOEFPTR src_block = src_buf[row][col];
                JCOEFPTR dst_block = dst_buf[row][col];

                dst_block[0] = src_block[0]; // Copy DC
                for (int i = 1; i < DCTSIZE2; ++i) {
                    acCoefficientValues.push_back(src_block[i]);
                    dst_block[i] = 0;
                    ++acCount;
                }
            }
        }
    }
    std::cerr << "[DEBUG] Extracted and zeroed " << acCount << " AC coefficients" << std::endl;

    // 5. Prepare for writing compressed output
    unsigned char* outbuffer = nullptr;
    unsigned long outsize = 0;
    jpeg_mem_dest(&cinfo, &outbuffer, &outsize);

    jpeg_write_coefficients(&cinfo, coeffs_cinfo);
    jpeg_finish_compress(&cinfo);

    if (outbuffer && outsize > 0) {
        criticalData.assign(outbuffer, outbuffer + outsize);
        free(outbuffer); // malloc'd by libjpeg
        std::cerr << "[DEBUG] Assigned criticalData, size=" << criticalData.size() << std::endl;
    } else {
        std::cerr << "[ERROR] jpeg_write_coefficients produced no output" << std::endl;
    }

    // 6. Cleanup
    jpeg_destroy_compress(&cinfo);
    jpeg_finish_decompress(&dinfo);
    jpeg_destroy_decompress(&dinfo);
    std::cerr << "[DEBUG] splitACCoefficientsExact: finished" << std::endl;
}



std::vector<uint8_t> JpegFileHandler::rebuildJPEGFromCriticalData(const std::vector<uint8_t>& critData, const std::vector<int16_t>& acData) {
    std::cerr << "[DEBUG] rebuildJPEGFromCriticalData: called, critData.size=" << critData.size() << ", acData.size=" << acData.size() << std::endl;
    // 1. Decompress the critical data (DC only)
    jpeg_decompress_struct dinfo;
    jpeg_error_mgr jerr;
    dinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&dinfo);
    std::cerr << "[DEBUG] Created decompress struct" << std::endl;
    jpeg_mem_src(&dinfo, critData.data(), critData.size());
    std::cerr << "[DEBUG] Set memory source" << std::endl;
    jpeg_read_header(&dinfo, TRUE);
    std::cerr << "[DEBUG] Read JPEG header" << std::endl;

    // Read the coefficient arrays (contains DC only)
    jvirt_barray_ptr* coeffs_dinfo = jpeg_read_coefficients(&dinfo);
    std::cerr << "[DEBUG] Read coefficients from decompressor" << std::endl;

    // 2. Prepare compressor for writing the full JPEG
    jpeg_compress_struct cinfo;
    jpeg_error_mgr cerr;
    cinfo.err = jpeg_std_error(&cerr);
    jpeg_create_compress(&cinfo);
    std::cerr << "[DEBUG] Created compress struct" << std::endl;

    // Copy parameters from the DC-only decompressor
    jpeg_copy_critical_parameters(&dinfo, &cinfo);
    std::cerr << "[DEBUG] Copied critical parameters" << std::endl;

    // Allocate *new* virtual coefficient arrays for the compressor
    jvirt_barray_ptr* coeffs_cinfo = (jvirt_barray_ptr*)
        (*cinfo.mem->alloc_small)((j_common_ptr)&cinfo, JPOOL_IMAGE, sizeof(jvirt_barray_ptr) * dinfo.num_components);

    for (int comp = 0; comp < dinfo.num_components; ++comp) {
        jpeg_component_info* sci = &dinfo.comp_info[comp];
        JDIMENSION h = sci->height_in_blocks;
        JDIMENSION w = sci->width_in_blocks;
        coeffs_cinfo[comp] = cinfo.mem->request_virt_barray((j_common_ptr)&cinfo, JPOOL_IMAGE, TRUE, w, h, 1);
        std::cerr << "[DEBUG] Allocated virt_barray for comp " << comp << ", w=" << w << ", h=" << h << std::endl;
    }

    // 3. Copy DC coefficients from decompressor and insert AC coefficients into the compressor's arrays
    size_t ac_index = 0;
    for (int comp = 0; comp < dinfo.num_components; ++comp) {
        jpeg_component_info* ci = &dinfo.comp_info[comp];
        JDIMENSION h = ci->height_in_blocks;
        JDIMENSION w = ci->width_in_blocks;

        JBLOCKARRAY src_buf = dinfo.mem->access_virt_barray((j_common_ptr)&dinfo, coeffs_dinfo[comp], 0, h, FALSE); // Read from dinfo's arrays (DC only)
        JBLOCKARRAY dst_buf = cinfo.mem->access_virt_barray((j_common_ptr)&cinfo, coeffs_cinfo[comp], 0, h, TRUE);  // Write to cinfo's arrays

        for (JDIMENSION row = 0; row < h; ++row) {
            for (JDIMENSION col = 0; col < w; ++col) {
                JCOEFPTR src_block = src_buf[row][col]; // This block only has DC, ACs are zero
                JCOEFPTR dst_block = dst_buf[row][col];

                // Copy DC coefficient from the DC-only source block
                dst_block[0] = src_block[0];

                // Insert AC coefficients from the stored acData
                for (int i = 1; i < DCTSIZE2; ++i) {
                    if (ac_index < acData.size()) {
                        dst_block[i] = acData[ac_index++];
                    } else {
                        std::cerr << "[DEBUG] Warning: Not enough AC data for block " << comp << "," << row << "," << col << std::endl;
                        dst_block[i] = 0; // Default to zero if data is missing
                    }
                }
            }
        }
    }
    std::cerr << "[DEBUG] Inserted " << ac_index << " AC coefficients" << std::endl;

    // 4. Write out the full JPEG using the compressor's arrays
    unsigned char* outbuffer = nullptr;
    unsigned long outsize = 0;
    jpeg_mem_dest(&cinfo, &outbuffer, &outsize);
    std::cerr << "[DEBUG] Set memory destination for compressor" << std::endl;

    jpeg_write_coefficients(&cinfo, coeffs_cinfo); // Use the compressor's arrays
    std::cerr << "[DEBUG] Wrote coefficients to compressor" << std::endl;

    jpeg_finish_compress(&cinfo);
    std::cerr << "[DEBUG] Finished compression" << std::endl;

    std::vector<uint8_t> result;
    if (outbuffer && outsize > 0) {
        result.assign(outbuffer, outbuffer + outsize);
        free(outbuffer); // jpeg_mem_dest allocates with malloc
        std::cerr << "[DEBUG] Assigned result, size=" << result.size() << std::endl;
    } else {
       std::cerr << "Error: rebuildJPEG produced no output." << std::endl;
    }

    // 5. Clean up libjpeg structures
    jpeg_destroy_compress(&cinfo);
    jpeg_finish_decompress(&dinfo); // Finish decompressing before destroying
    jpeg_destroy_decompress(&dinfo);
    std::cerr << "[DEBUG] rebuildJPEGFromCriticalData: finished" << std::endl;
    return result;
}

ResultCode JpegFileHandler::writeFile(const char* mappingPath, const char* buffer, size_t size, off_t offset) {
    std::string basePath(mappingPath);
    const std::string suffix = ".mapping";
    if (basePath.size() >= suffix.size() && basePath.compare(basePath.size() - suffix.size(), suffix.size(), suffix) == 0) {
        basePath = basePath.substr(0, basePath.size() - suffix.size());
    }

    if (offset == 0) {
        splitACCoefficientsExact(buffer, size);

        std::ofstream crit(basePath + ".crit", std::ios::binary);
        crit.write(reinterpret_cast<const char*>(criticalData.data()), criticalData.size());

        std::ofstream noncrit(basePath + ".noncrit", std::ios::binary);
        noncrit.write(reinterpret_cast<const char*>(acCoefficientValues.data()), acCoefficientValues.size() * sizeof(int16_t));
    }
    return ResultCode::SUCCESS;
}

ResultCode JpegFileHandler::readFile(const char* mappingPath, char* buffer, size_t size, off_t offset) {
    std::string basePath(mappingPath);
    const std::string suffix = ".mapping";
    if (basePath.size() >= suffix.size() && basePath.compare(basePath.size() - suffix.size(), suffix.size(), suffix) == 0) {
        basePath = basePath.substr(0, basePath.size() - suffix.size());
    }

    std::ifstream critFile(basePath + ".crit", std::ios::binary);
    std::vector<uint8_t> critData((std::istreambuf_iterator<char>(critFile)), {});

    std::ifstream acFile(basePath + ".noncrit", std::ios::binary);
    acFile.seekg(0, std::ios::end);
    size_t fileSize = acFile.tellg();
    acFile.seekg(0, std::ios::beg);
    std::vector<int16_t> acData(fileSize / sizeof(int16_t));
    acFile.read(reinterpret_cast<char*>(acData.data()), fileSize);

    std::vector<uint8_t> full = rebuildJPEGFromCriticalData(critData, acData);
    if (offset + size <= full.size()) {
        std::memcpy(buffer, full.data() + offset, size);
        return ResultCode::SUCCESS;
    }
    return ResultCode::FAILURE;
}

ResultCode JpegFileHandler::createMapping(const char* buffer, size_t size) {
    // For this approach, mapping is not used, so just return SUCCESS
    return ResultCode::SUCCESS;
}
