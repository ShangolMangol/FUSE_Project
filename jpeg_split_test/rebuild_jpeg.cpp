#include <iostream>
#include <fstream>
#include <vector>
#include <cstdio>
#include <jpeglib.h>
#include <setjmp.h>

struct my_error_mgr {
    jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};
typedef my_error_mgr* my_error_ptr;

extern "C" {
    METHODDEF(void) my_error_exit(j_common_ptr cinfo) {
        my_error_ptr myerr = (my_error_ptr)cinfo->err;
        (*cinfo->err->output_message)(cinfo);
        longjmp(myerr->setjmp_buffer, 1);
    }
}

bool read_crit(const std::string& path, std::vector<unsigned char>& header_data, std::vector<int16_t>& dc_coeffs) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    unsigned char b1 = 0, b2 = 0;
    in.read((char*)&b1, 1);
    in.read((char*)&b2, 1);
    header_data.push_back(b1);
    header_data.push_back(b2);

    bool saw_sos = false;
    bool saw_eoi = false;

    while (in && !saw_eoi) {
        in.read((char*)&b1, 1);
        if (!in) break;
        header_data.push_back(b1);

        if (b1 != 0xFF) continue;

        in.read((char*)&b2, 1);
        if (!in) break;
        header_data.push_back(b2);

        if (b2 == 0xDA) {  // Start of Scan
            // Read SOS size
            unsigned char size_buf[2];
            in.read((char*)size_buf, 2);
            header_data.push_back(size_buf[0]);
            header_data.push_back(size_buf[1]);
            int size = (size_buf[0] << 8) + size_buf[1];

            // Read SOS data
            std::vector<unsigned char> sos_data(size - 2);
            in.read((char*)sos_data.data(), size - 2);
            header_data.insert(header_data.end(), sos_data.begin(), sos_data.end());

            // Now stream compressed image data until EOI
            while (in.read((char*)&b1, 1)) {
                header_data.push_back(b1);
                if (b1 == 0xFF) {
                    if (in.read((char*)&b2, 1)) {
                        header_data.push_back(b2);
                        if (b2 == 0xD9) {
                            saw_eoi = true;
                            break;
                        }
                    } else {
                        break;
                    }
                }
            }

            break;  // Done reading compressed data + EOI
        }

        // Not SOS yet — regular marker
        unsigned char size_buf[2];
        in.read((char*)size_buf, 2);
        header_data.insert(header_data.end(), size_buf, size_buf + 2);
        int size = (size_buf[0] << 8) + size_buf[1];
        std::vector<unsigned char> segment(size - 2);
        in.read((char*)segment.data(), size - 2);
        header_data.insert(header_data.end(), segment.begin(), segment.end());
    }

    // Now read the DC coefficients after EOI
    int16_t dc;
    while (in.read((char*)&dc, sizeof(int16_t))) {
        dc_coeffs.push_back(dc);
    }

    return saw_eoi;
}



bool read_noncrit(const std::string& path, std::vector<int16_t>& ac_coeffs) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    int16_t ac;
    while (in.read((char*)&ac, sizeof(int16_t))) {
        ac_coeffs.push_back(ac);
    }
    return true;
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <.crit> <.noncrit> <output.jpg>" << std::endl;
        return 1;
    }

    const char* crit_path = argv[1];
    const char* noncrit_path = argv[2];
    const char* output_path = argv[3];

    std::vector<unsigned char> header_data;
    std::vector<int16_t> dc_coeffs;
    std::vector<int16_t> ac_coeffs;

    if (!read_crit(crit_path, header_data, dc_coeffs)) {
        std::cerr << "Failed to read .crit file\n";
        return 1;
    }

    if (!read_noncrit(noncrit_path, ac_coeffs)) {
        std::cerr << "Failed to read .noncrit file\n";
        return 1;
    }

    jpeg_decompress_struct dinfo;
    my_error_mgr jerr;
    dinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;
    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&dinfo);
        std::cerr << "JPEG error!\n";
        return 1;
    }

    jpeg_create_decompress(&dinfo);
    jpeg_mem_src(&dinfo, header_data.data(), header_data.size());
    jpeg_read_header(&dinfo, TRUE);
    jvirt_barray_ptr* coef_arrays = jpeg_read_coefficients(&dinfo);

    size_t dc_index = 0, ac_index = 0;

    for (int comp = 0; comp < dinfo.num_components; comp++) {
        jpeg_component_info* compptr = dinfo.comp_info + comp;
        for (JDIMENSION row = 0; row < compptr->height_in_blocks; row++) {
            JBLOCKARRAY buffer = (dinfo.mem->access_virt_barray)
                ((j_common_ptr)&dinfo, coef_arrays[comp], row, 1, TRUE);
            for (JDIMENSION col = 0; col < compptr->width_in_blocks; col++) {
                JBLOCK* block = &buffer[0][col];
                block[0][0] = dc_index < dc_coeffs.size() ? dc_coeffs[dc_index++] : 0;
                for (int i = 1; i < DCTSIZE2; i++) {
                    block[0][i] = ac_index < ac_coeffs.size() ? ac_coeffs[ac_index++] : 0;
                }
            }
        }
    }

    jpeg_compress_struct cinfo;
    jpeg_error_mgr jcerr;
    cinfo.err = jpeg_std_error(&jcerr);
    jpeg_create_compress(&cinfo);
    FILE* outfile = fopen(output_path, "wb");
    if (!outfile) {
        std::cerr << "Failed to open output file\n";
        return 1;
    }

    jpeg_stdio_dest(&cinfo, outfile);
    cinfo.image_width = dinfo.image_width;
    cinfo.image_height = dinfo.image_height;
    cinfo.input_components = dinfo.num_components;
    cinfo.in_color_space = dinfo.jpeg_color_space;

    jpeg_set_defaults(&cinfo);
    jpeg_copy_critical_parameters(&dinfo, &cinfo);
    jpeg_write_coefficients(&cinfo, coef_arrays);
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    jpeg_finish_decompress(&dinfo);
    jpeg_destroy_decompress(&dinfo);
    fclose(outfile);

    std::cout << "JPEG rebuilt to: " << output_path << std::endl;
    return 0;
}
