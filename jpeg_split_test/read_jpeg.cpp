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

bool extract_header(const char* filename, std::vector<unsigned char>& header_data) {
    FILE* file = fopen(filename, "rb");
    if (!file) return false;

    unsigned char buffer[2];
    fread(buffer, 1, 2, file);  // Should be 0xFFD8
    header_data.push_back(buffer[0]);
    header_data.push_back(buffer[1]);

    bool saw_sos = false;

    while (fread(buffer, 1, 2, file) == 2) {
        header_data.push_back(buffer[0]);
        header_data.push_back(buffer[1]);
        if (buffer[0] != 0xFF) break;

        if (buffer[1] == 0xDA) {  // Start of Scan
            // Read SOS segment size and data
            unsigned char size_buf[2];
            fread(size_buf, 1, 2, file);
            header_data.push_back(size_buf[0]);
            header_data.push_back(size_buf[1]);
            int size = (size_buf[0] << 8) + size_buf[1];

            std::vector<unsigned char> sos_data(size - 2);
            fread(sos_data.data(), 1, size - 2, file);
            header_data.insert(header_data.end(), sos_data.begin(), sos_data.end());

            saw_sos = true;
            break;
        }

        // Not SOS yet — keep reading segments
        unsigned char size_buf[2];
        fread(size_buf, 1, 2, file);
        header_data.push_back(size_buf[0]);
        header_data.push_back(size_buf[1]);
        int size = (size_buf[0] << 8) + size_buf[1];
        std::vector<unsigned char> segment(size - 2);
        fread(segment.data(), 1, size - 2, file);
        header_data.insert(header_data.end(), segment.begin(), segment.end());
    }

    // If SOS was found, now scan for EOI marker 0xFFD9
    if (saw_sos) {
        while (fread(buffer, 1, 2, file) == 2) {
            header_data.push_back(buffer[0]);
            header_data.push_back(buffer[1]);
            if (buffer[0] == 0xFF && buffer[1] == 0xD9) {
                break; // EOI found
            }
        }
    }

    fclose(file);
    return true;
}


int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <jpeg_file>" << std::endl;
        return 1;
    }

    const char* filename = argv[1];
    std::string crit_file = std::string(filename) + ".crit";
    std::string noncrit_file = std::string(filename) + ".noncrit";

    jpeg_decompress_struct cinfo;
    my_error_mgr jerr;
    FILE* infile = fopen(filename, "rb");

    if (!infile) {
        std::cerr << "Cannot open " << filename << std::endl;
        return 1;
    }

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;
    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        fclose(infile);
        std::cerr << "JPEG error!" << std::endl;
        return 1;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, infile);
    jpeg_read_header(&cinfo, TRUE);
    jvirt_barray_ptr* coef_arrays = jpeg_read_coefficients(&cinfo);

    std::vector<unsigned char> header_data;
    if (!extract_header(filename, header_data)) {
        std::cerr << "Header extraction failed\n";
        return 1;
    }

    std::ofstream crit_out(crit_file, std::ios::binary);
    std::ofstream noncrit_out(noncrit_file, std::ios::binary);

    crit_out.write((char*)header_data.data(), header_data.size());

    for (int comp = 0; comp < cinfo.num_components; comp++) {
        jpeg_component_info* compptr = cinfo.comp_info + comp;
        for (JDIMENSION row = 0; row < compptr->height_in_blocks; row++) {
            JBLOCKARRAY buffer = (cinfo.mem->access_virt_barray)
                ((j_common_ptr)&cinfo, coef_arrays[comp], row, 1, FALSE);
            for (JDIMENSION col = 0; col < compptr->width_in_blocks; col++) {
                JBLOCK* block = &buffer[0][col];
                // Write DC to .crit
                int16_t dc = block[0][0];
                crit_out.write((char*)&dc, sizeof(int16_t));
                // Write AC to .noncrit
                for (int i = 1; i < DCTSIZE2; i++) {
                    int16_t ac = block[0][i];
                    noncrit_out.write((char*)&ac, sizeof(int16_t));
                }
            }
        }
    }

    crit_out.close();
    noncrit_out.close();

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(infile);

    std::cout << "JPEG split into: " << crit_file << " and " << noncrit_file << std::endl;
    return 0;
}
