/*
 * jpeg_splitter.cpp – Split a JPEG into three binary parts:
 *   1. <name>.hdr  – JPEG header + metadata (all markers up to and including the first SOS)
 *   2. <name>.dc   – DC coefficients,  one 16‑bit big‑endian word per MCU block
 *   3. <name>.ac   – AC coefficients, sixty‑three 16‑bit big‑endian words per MCU block
 *
 * Build: g++ jpeg_splitter.cpp -o jpeg_splitter -ljpeg
 * Usage: ./jpeg_splitter image.jpg
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <jpeglib.h>
#include <setjmp.h>

//------------------------------------------------------------------
//  Helper – libjpeg error handling
//------------------------------------------------------------------
struct my_error_mgr {
    jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};
using my_error_ptr = my_error_mgr*;

extern "C" {
    static void my_error_exit(j_common_ptr cinfo) {
        my_error_ptr myerr = reinterpret_cast<my_error_ptr>(cinfo->err);
        (*cinfo->err->output_message)(cinfo);
        longjmp(myerr->setjmp_buffer, 1);
    }
}

//------------------------------------------------------------------
//  Helper – big‑endian write of int16_t values
//------------------------------------------------------------------
static inline void write_be16(std::ofstream& out, int16_t value) {
    uint16_t u = static_cast<uint16_t>(value);
    unsigned char b[2] = { static_cast<unsigned char>((u >> 8) & 0xFF),
                           static_cast<unsigned char>(u & 0xFF) };
    out.write(reinterpret_cast<char*>(b), 2);
}

//------------------------------------------------------------------
//  Extract all marker segments up to (and incl.) the first SOS (FF DA)
//------------------------------------------------------------------
bool extract_header(const char* filename, std::vector<unsigned char>& header) {
    FILE* fp = std::fopen(filename, "rb");
    if (!fp) return false;

    unsigned char buf[2];

    // Expect SOI (FF D8)
    if (std::fread(buf, 1, 2, fp) != 2 || buf[0] != 0xFF || buf[1] != 0xD8) {
        std::cerr << "Not a JPEG (missing SOI)\n";
        std::fclose(fp);
        return false;
    }
    header.push_back(buf[0]); header.push_back(buf[1]);

    bool found_sos = false;
    while (std::fread(buf, 1, 2, fp) == 2) {
        header.push_back(buf[0]); header.push_back(buf[1]);
        if (buf[0] != 0xFF) break;              // corrupt stream
        if (buf[1] == 0xD9) break;              // EOI – shouldn’t happen before SOS

        // SOS reached? copy its payload then stop
        if (buf[1] == 0xDA) {
            unsigned char size_buf[2];
            if (std::fread(size_buf, 1, 2, fp) != 2) break;
            header.push_back(size_buf[0]); header.push_back(size_buf[1]);
            int size = (size_buf[0] << 8) | size_buf[1];
            std::vector<unsigned char> payload(size - 2);
            if (std::fread(payload.data(), 1, size - 2, fp) != static_cast<size_t>(size - 2)) break;
            header.insert(header.end(), payload.begin(), payload.end());
            found_sos = true;
            break;   // *do not* continue into entropy‑coded data
        }

        // Regular marker: read its [size] + payload
        unsigned char size_buf[2];
        if (std::fread(size_buf, 1, 2, fp) != 2) break;
        header.push_back(size_buf[0]); header.push_back(size_buf[1]);
        int size = (size_buf[0] << 8) | size_buf[1];
        std::vector<unsigned char> payload(size - 2);
        if (std::fread(payload.data(), 1, size - 2, fp) != static_cast<size_t>(size - 2)) break;
        header.insert(header.end(), payload.begin(), payload.end());
    }

    std::fclose(fp);
    return found_sos;
}

//------------------------------------------------------------------
//  Main
//------------------------------------------------------------------
int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <jpeg_file>\n";
        return 1;
    }

    const char* infile_name = argv[1];
    std::string base = infile_name;
    std::string hdr_file = base + ".hdr";
    std::string dc_file = base + ".dc";
    std::string ac_file = base + ".ac";

    //------------------------------------------------------------------
    // 1. Extract header / metadata
    //------------------------------------------------------------------
    std::vector<unsigned char> header_data;
    if (!extract_header(infile_name, header_data)) {
        std::cerr << "Failed to extract header from " << infile_name << "\n";
        return 1;
    }
    std::ofstream hdr_out(hdr_file, std::ios::binary);
    hdr_out.write(reinterpret_cast<char*>(header_data.data()), header_data.size());
    hdr_out.close();

    //------------------------------------------------------------------
    // 2. Open JPEG with libjpeg and read coefficient arrays
    //------------------------------------------------------------------
    FILE* fp = std::fopen(infile_name, "rb");
    if (!fp) {
        std::cerr << "Cannot open " << infile_name << "\n";
        return 1;
    }

    jpeg_decompress_struct cinfo{};
    my_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;

    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        std::fclose(fp);
        std::cerr << "JPEG fatal error\n";
        return 1;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fp);
    jpeg_read_header(&cinfo, TRUE);
    jvirt_barray_ptr* coef_arrays = jpeg_read_coefficients(&cinfo);

    //------------------------------------------------------------------
    // 3. Write DC and AC streams
    //------------------------------------------------------------------
    std::ofstream dc_out(dc_file, std::ios::binary);
    std::ofstream ac_out(ac_file, std::ios::binary);

    for (int comp = 0; comp < cinfo.num_components; ++comp) {
        jpeg_component_info* compptr = cinfo.comp_info + comp;
        for (JDIMENSION row = 0; row < compptr->height_in_blocks; ++row) {
            JBLOCKARRAY buffer = (cinfo.mem->access_virt_barray)
                (reinterpret_cast<j_common_ptr>(&cinfo), coef_arrays[comp], row, 1, FALSE);
            for (JDIMENSION col = 0; col < compptr->width_in_blocks; ++col) {
                JBLOCK* blk = &buffer[0][col];
                // DC
                write_be16(dc_out, blk->data[0]);
                // AC (1..63)
                for (int i = 1; i < DCTSIZE2; ++i) {
                    write_be16(ac_out, blk->data[i]);
                }
            }
        }
    }

    dc_out.close();
    ac_out.close();

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    std::fclose(fp);

    std::cout << "Split complete:\n  " << hdr_file << " (" << header_data.size() << " bytes)\n  "
              << dc_file << " (DC coefficients)\n  " << ac_file << " (AC coefficients)\n";

    return 0;
}
