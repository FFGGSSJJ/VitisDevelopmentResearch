/**
* Copyright (C) 2019-2021 Xilinx, Inc
*
* Licensed under the Apache License, Version 2.0 (the "License"). You may
* not use this file except in compliance with the License. A copy of the
* License is located at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
* WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
* License for the specific language governing permissions and limitations
* under the License.
*/

// OpenCL utility layer include
#include <algorithm>
#include <chrono>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iosfwd>
#include <iostream>
#include <unistd.h>
#include <vector>
#include <x86intrin.h>

#define KB  1024
#define MB  1024 * KB

size_t max_buffer = 512 * 1024 * 1024;
size_t min_buffer = 4 * 1024;
size_t max_size = 512 * 1024 * 1024; // 512MB


void flush_cachelines(void* ptr)
{
    const int LINESIZE = 64;
    const char* p = (const char*)ptr;
    uintptr_t endline = ((uintptr_t)ptr + max_size - 1) | (LINESIZE-1);

    do {   // flush while p is in a cache line that contains any of the struct
         _mm_clflush(p);
          p += LINESIZE;
    } while(p <= (const char*)endline);
}



int ssd_to_dram(int& nvmeFd) {
    int err;
    int ret = 0;
    size_t vector_size_bytes = max_buffer;

    /* Get the size of the file */
    size_t datasize = 0;
    size_t bufsize = 4 * KB;

    /* Allocate space in CUP DRAM */
    int32_t* dram_ptr = (int32_t*)malloc(max_buffer);

    std::string fname = "/smartssd/gf9/matrix_band/int4096x4096";
    int fd = open(fname.c_str(), O_NOATIME | O_RDWR, 0644);
    // O_DIRECT
    
    /* Start transfer using various buffer sizes */
    std::cout << "Start  Write of various buffer sizes from SSD to CPU DRAM\n" << std::endl;
    for (datasize = 128 * MB; datasize <= max_size; datasize *= 2) {
        std::cout << "\n------------------------------------------\n";
        std::cout << "   Data Size: " << datasize/1024/1024 << "MB";
        std::cout << "\n------------------------------------------\n";
        for (bufsize = 4 * KB; bufsize <= datasize; bufsize *= 2) {

            int iter = datasize/bufsize;

            std::chrono::high_resolution_clock::time_point p2pStart = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < iter; i++) {
                ret = pread(fd, (void*)dram_ptr, bufsize, 0);
                if (ret == -1) {
                    std::cout << "read() failed, err: " << ret << ", line: " << __LINE__ << std::endl;
                    return EXIT_FAILURE;
                }
            }
            std::chrono::high_resolution_clock::time_point p2pEnd = std::chrono::high_resolution_clock::now();
            long p2pTime = std::chrono::duration_cast<std::chrono::microseconds>(p2pEnd - p2pStart).count();
            double dnsduration = (double)p2pTime;
            double dsduration = dnsduration / ((double)1000000);
            double gbpersec = (iter * bufsize / dsduration) / ((double)1024 * 1024 * 1024);
            std::cout << "Buffer = " << bufsize << " Iterations = " << iter << " Throughput = " << std::setprecision(2)
                    << std::fixed << gbpersec << "GB/s\n";
            
            flush_cachelines((void*)dram_ptr);
        }
    }
    return 0;
}

void dram_to_ssd(int& nvmeFd) {
    int err;
    size_t vector_size_bytes = max_buffer;

    /* Allocate space in CUP DRAM */
    int32_t* dram_ptr = (int32_t*)calloc(1, max_buffer);


    /* Get the size of the file */
    size_t datasize = 0;
    size_t bufsize = 4 * KB;

    std::cout << "Start write of various buffer sizes from CPU DRAM to SSD\n" << std::endl;
    for (datasize = 128 * MB; datasize <= max_size; datasize *= 2) {
        std::cout << "\n------------------------------------------\n";
        std::cout << "   Data Size: " << datasize/1024/1024 << "MB";
        std::cout << "\n------------------------------------------\n";
        for (bufsize = 4 * KB; bufsize <= datasize; bufsize *= 2) {

            int iter = datasize / bufsize;
            
            std::chrono::high_resolution_clock::time_point p2pStart = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < iter; i++) {
                if (pwrite(nvmeFd, (void*)dram_ptr, bufsize, 0) <= 0) {
                    std::cout << "wirte failed\n";
                    exit(EXIT_FAILURE);
                }
            }
            std::chrono::high_resolution_clock::time_point p2pEnd = std::chrono::high_resolution_clock::now();
            long p2pTime = std::chrono::duration_cast<std::chrono::microseconds>(p2pEnd - p2pStart).count();
            double dnsduration = (double)p2pTime;
            double dsduration = dnsduration / ((double)1000000);
            double gbpersec = (iter * bufsize / dsduration) / ((double)1024 * 1024 * 1024);
            std::cout << "Buffer = " << bufsize << " Iterations = " << iter << " Throughput = " << std::setprecision(2)
                    << std::fixed << gbpersec << "GB/s\n";

            flush_cachelines((void*)dram_ptr);
        }
    }
}

int main(int argc, char** argv) {

    // Read settings
    std::string filepath = "/smartssd/gf9/matrix_band/int4096x4096";
    std::string filename = filepath;
    int nvmeFd = -1;


    // P2P transfer from host to SSD
    std::cout << "############################################################\n";
    std::cout << "             Reading data from SSD to CPU DRAM              \n";
    std::cout << "############################################################\n";
    // Get access to the NVMe SSD.
    nvmeFd = open(filename.c_str(), O_RDWR | O_DIRECT);
    if (nvmeFd < 0) {
        std::cerr << "ERROR: open " << filename << "failed: " << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "INFO: Successfully opened NVME SSD " << filename << std::endl;
    int ret = 0;
    ret = ssd_to_dram(nvmeFd);
    (void)close(nvmeFd);
    if (ret != 0) return EXIT_FAILURE;


    // transfer from SSD to host
    std::cout << "############################################################\n";
    std::cout << "           Writing data from CPU DRAM to SSD                \n";
    std::cout << "############################################################\n";

    nvmeFd = open(filename.c_str(), O_RDWR | O_DIRECT);
    if (nvmeFd < 0) {
        std::cerr << "ERROR: open " << filename << "failed: " << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "INFO: Successfully opened NVME SSD " << filename << std::endl;

    dram_to_ssd(nvmeFd);
    (void)close(nvmeFd);


    std::cout << "TEST PASSED" << std::endl;
    return EXIT_SUCCESS;
}
