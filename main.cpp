#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <cstddef>
#include <iterator>
#include <limits>
#include <ostream>
#include <utility>
#include <numeric>
#include <unistd.h>
#include <stdlib.h>
#include <random>

// General definitions
#define KILOBYTE 1024
#define MEGABYTE 1024 * KILOBYTE
#define GIGABYTE 1024 * MEGABYTE

#define MIN_STRIDE 32
#define MAX_STRIDE 512

// Benchmark parameters
#define ARR_LENGTH (uint64_t) 2 * GIGABYTE
#define N_ACCESSES 2000000

// Precision parameters
#define PRECISION 1e-3
#define FAKE_MULTIPLE 10000000
#define REQUIRED_N_CONVERGED_RUNS 3
#define TOTAL_RUNS_THRESHOLD 30
#define NOT_CONVERGED_VALUE std::numeric_limits<double>::infinity()


volatile uint8_t* allocate_array() {
    long page_size = sysconf(_SC_PAGE_SIZE);
    void * arr = aligned_alloc(page_size, ARR_LENGTH);
    if (arr == nullptr) {
        std::cout << "Failed to allocate array of length " << ARR_LENGTH << std::endl;
        std::exit(1);
    }
    return (uint8_t *) arr;
}


void clear_array(volatile uint8_t* arr) {
    for (int i = 0; i < ARR_LENGTH; i++) {
        arr[i] = 0;
    }
}


int generate_chain(volatile uint8_t* arr, int stride) {
    clear_array(arr);

    int double_stride = 2 * stride;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distr(0, ARR_LENGTH / double_stride);

    int chain_length = 1; // take into account the first read: arr[0]
    auto prev_addr = arr;
    for (int i = 0; i < N_ACCESSES; i++) {
        int random_arr_index = distr(gen) * double_stride;
        auto base_addr = arr + random_arr_index;
        auto shifted_addr = base_addr + stride;
        auto first_addr = shifted_addr;
        auto second_addr = base_addr;

        if (* (volatile uint64_t *) first_addr != 0 || random_arr_index + stride >= ARR_LENGTH) {
            continue;
        }
        chain_length += 2;

        * (volatile uint64_t *) prev_addr = (uint64_t) first_addr;
        * (volatile uint64_t *) first_addr = (uint64_t) second_addr;
        prev_addr = second_addr;
    }
    * (volatile uint64_t *) prev_addr = 0;

    // std::cout << "Generated chain length=" << chain_length << std::endl;
    return chain_length;
}


double benchmark(volatile uint8_t* arr) {
    volatile uint64_t * prev_value = nullptr;
    auto start = std::chrono::high_resolution_clock::now();
    // >>> begin benchmark
    auto value = (volatile uint64_t *) arr;
    while (value != nullptr) {
        prev_value = value;
        value = (volatile uint64_t *) *value;
    }
    // <<< end benchmark
    auto end = std::chrono::high_resolution_clock::now();
    const std::chrono::duration<double> diff = end - start;
    std::cerr << "benchmark acc=" << prev_value << std::endl;
    return diff.count();
}


void flush_caches(volatile uint8_t* dummy_arr) {
    for (uint64_t i = 0; i < ARR_LENGTH; i++) {
        dummy_arr[i] = i;
    }
}


double run_benchmark_once(volatile uint8_t* arr, volatile uint8_t* dummy_arr, int stride) {
    auto chain_length = generate_chain(arr, stride);
    // std::cerr << "Generated chain of length " << chain_length << std::endl;
    flush_caches(dummy_arr);
    return benchmark(arr) * FAKE_MULTIPLE / chain_length;
}


double run_benchmark_until_converges(volatile uint8_t* arr, volatile uint8_t* dummy_arr, int stride) {
    int n = 0;
    double sum = 0;
    double mean = 0;
    int n_successes = 0;
    while (n < TOTAL_RUNS_THRESHOLD) {
        auto bench_result = run_benchmark_once(arr, dummy_arr, stride);
        n++;
        sum += bench_result;
        auto cur_mean = sum / n;
        auto current_err = abs(cur_mean - mean);
        std::cerr << "Run "<< n << ": Current benchmark results = " << cur_mean << ", current error = " << current_err << std::endl;
        if (current_err < PRECISION) {
            n_successes++;
            if (n_successes >= REQUIRED_N_CONVERGED_RUNS) {
                std::cerr << "Converged to " << cur_mean << " on the " << n << "-th iteration" << std::endl;
                return cur_mean;
            }
        } else {
            n_successes = 0;
        }
        mean = cur_mean;
    }
    // Did not converge
    return NOT_CONVERGED_VALUE;
}


int main() {
    auto arr = allocate_array();
    auto dummy_arr = allocate_array();

    // std::cerr << "Array spans " << (uint64_t*) arr << ".." << (uint64_t *) (arr + ARR_LENGTH - 1) << std::endl;
    std::cout << "stride,result" << std::endl;
    for (int stride = MIN_STRIDE; stride <= MAX_STRIDE; stride *= 2) {
        auto result = run_benchmark_until_converges(arr, dummy_arr, stride);
        std::cout << stride << "," << result << std::endl;
        if (result == NOT_CONVERGED_VALUE) {
            std::cout << "Cache line size = " << stride << std::endl;
            std::exit(0);
        }
    }

    return 0;
}
