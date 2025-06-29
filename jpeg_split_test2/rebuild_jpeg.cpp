/*
 * jpeg_rebuilder.cpp – Reconstruct a JPEG from the three files produced by jpeg_splitter.cpp
 *
 *   Inputs  (big‑endian values expected):
 *       <base>.hdr   – header + metadata up to and including the first SOS
 *       <base>.dc    – DC coefficients, one 16‑bit BE per block
 *       <base>.ac    – AC coefficients, 63× 16‑bit BE per block
 *   Output:
 *       <output.jpg> – fully compressed JPEG produced by libjpeg, keeping the
 *                      original quant tables, sampling factors & colour space.
 *
 * Build: g++ jpeg_rebuilder.cpp -o jpeg_rebuilder -ljpeg
 * Usage: ./jpeg_rebuilder  input.hdr  input.dc  input.ac  output.jpg
 *        (or ./jpeg_rebuilder  base_name  output.jpg  – will append .hdr/.dc/.ac automatically)
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
//  libjpeg error handling helper
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
//  Helpers for big‑endian I/O
//------------------------------------------------------------------
static inline int16_t read_be16(std::ifstream& in) {
    unsigned char b[2];
    in.read(reinterpret_cast<char*>(b), 2);
    if (in.gcount() != 2) throw std::runtime_error("Unexpected EOF while reading 16‑bit value");
    return static_cast<int16_t>((b[0] << 8) | b[1]);
}

//------------------------------------------------------------------
//  Read an entire file into memory
//------------------------------------------------------------------
static std::vector<unsigned char> slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open " + path);
    f.seekg(0, std::ios::end);
    std::streamoff sz = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<unsigned char> buf(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    if (f.gcount() != sz) throw std::runtime_error("Failed to read whole file " + path);
    return buf;
}

//------------------------------------------------------------------
//  Main
//------------------------------------------------------------------
int main(int argc, char** argv) {
    std::string hdr_path, dc_path, ac_path, out_path;

    if (argc == 5) {
        hdr_path = argv[1];
        dc_path  = argv[2];
        ac_path  = argv[3];
        out_path = argv[4];
    } else if (argc == 3) {
        std::string base = argv[1];
        hdr_path = base + ".hdr";
        dc_path  = base + ".dc";
        ac_path  = base + ".ac";
        out_path = argv[2];
    } else {
        std::cerr << "Usage: " << argv[0] << "  <hdr> <dc> <ac> <out.jpg>\n";
        std::cerr << "   or  : " << argv[0] << "  <base> <out.jpg>   (will load base.hdr|dc|ac)\n";
        return 1;
    }

    //------------------------------------------------------------------
    // 1. Load header in memory and parse it with libjpeg (decompress struct)
    //------------------------------------------------------------------
    std::vector<unsigned char> hdr_buf = slurp(hdr_path);

    jpeg_decompress_struct srcinfo{};
    my_error_mgr jerr_src;
    srcinfo.err = jpeg_std_error(&jerr_src.pub);
    jerr_src.pub.error_exit = my_error_exit;

    if (setjmp(jerr_src.setjmp_buffer)) {
        jpeg_destroy_decompress(&srcinfo);
        std::cerr << "Fatal error while parsing header." << std::endl;
        return 1;
    }

    jpeg_create_decompress(&srcinfo);
    jpeg_mem_src(&srcinfo, hdr_buf.data(), static_cast<unsigned long>(hdr_buf.size()));
    jpeg_read_header(&srcinfo, TRUE);  // TRUE => process tables

    //------------------------------------------------------------------
    // 2. Prepare destination compressor and copy critical parameters
    //------------------------------------------------------------------
    jpeg_compress_struct dstinfo{};
    my_error_mgr jerr_dst;
    dstinfo.err = jpeg_std_error(&jerr_dst.pub);
    jerr_dst.pub.error_exit = my_error_exit;

    if (setjmp(jerr_dst.setjmp_buffer)) {
        jpeg_destroy_compress(&dstinfo);
        jpeg_destroy_decompress(&srcinfo);
        std::cerr << "Fatal error in compression stage." << std::endl;
        return 1;
    }

    jpeg_create_compress(&dstinfo);

    // Copy width, height, components, quant & Huffman tables, sampling factors …
    jpeg_copy_critical_parameters(&srcinfo, &dstinfo);

    //------------------------------------------------------------------
    // 3. Allocate virtual coefficient arrays in the compressor
    //------------------------------------------------------------------
    jvirt_barray_ptr* coef_arrays = reinterpret_cast<jvirt_barray_ptr*>(
        dstinfo.mem->alloc_small((j_common_ptr)&dstinfo, JPOOL_IMAGE,
                                  sizeof(jvirt_barray_ptr) * dstinfo.num_components));

    for (int comp = 0; comp < dstinfo.num_components; ++comp) {
        jpeg_component_info* ci = dstinfo.comp_info + comp;
        coef_arrays[comp] = dstinfo.mem->request_virt_barray(
            (j_common_ptr)&dstinfo, JPOOL_IMAGE, TRUE,
            ci->width_in_blocks, ci->height_in_blocks, (JDIMENSION)1);
    }

    //------------------------------------------------------------------
    // 4. Read DC & AC files and populate coefficient blocks
    //------------------------------------------------------------------
    std::ifstream dc_in(dc_path, std::ios::binary);
    if (!dc_in) { std::cerr << "Cannot open " << dc_path << std::endl; return 1; }
    std::ifstream ac_in(ac_path, std::ios::binary);
    if (!ac_in) { std::cerr << "Cannot open " << ac_path << std::endl; return 1; }

    try {
        for (int comp = 0; comp < dstinfo.num_components; ++comp) {
            jpeg_component_info* ci = dstinfo.comp_info + comp;
            for (JDIMENSION row = 0; row < ci->height_in_blocks; ++row) {
                JBLOCKARRAY rowptr = dstinfo.mem->access_virt_barray(
                    (j_common_ptr)&dstinfo, coef_arrays[comp], row, 1, TRUE);
                for (JDIMENSION col = 0; col < ci->width_in_blocks; ++col) {
                    JBLOCK* blk = &rowptr[0][col];
                    blk->data[0] = read_be16(dc_in);
                    for (int k = 1; k < DCTSIZE2; ++k) {
                        blk->data[k] = read_be16(ac_in);
                    }
                }
            }
        }
    }
    catch (const std::exception& ex) {
        std::cerr << "Error while reading coefficient files: " << ex.what() << std::endl;
        return 1;
    }

    dc_in.close();
    ac_in.close();

    //------------------------------------------------------------------
    // 5. Write the compressed JPEG
    //------------------------------------------------------------------
    FILE* outfile = std::fopen(out_path.c_str(), "wb");
    if (!outfile) { std::cerr << "Cannot create " << out_path << std::endl; return 1; }
    jpeg_stdio_dest(&dstinfo, outfile);

    jpeg_write_coefficients(&dstinfo, coef_arrays);
    jpeg_finish_compress(&dstinfo);

    //------------------------------------------------------------------
    // 6. Clean up
    //------------------------------------------------------------------
    jpeg_destroy_compress(&dstinfo);
    jpeg_destroy_decompress(&srcinfo);
    std::fclose(outfile);

    std::cout << "Rebuilt JPEG saved to " << out_path << std::endl;
    return 0;
}
