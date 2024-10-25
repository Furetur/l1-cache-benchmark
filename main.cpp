#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <iterator>
#include <limits>
#include <numeric>
#include <ostream>
#include <random>
#include <stdlib.h>
#include <unistd.h>
#include <utility>
#include <vector>

// General definitions
#define KILOBYTE 1024
#define MEGABYTE 1024 * KILOBYTE
#define GIGABYTE 1024 * MEGABYTE

// Search parameters
#define MIN_CACHELINE_SIZE 8
#define MAX_CACHELINE_SIZE 512
#define MIN_N_SETS 16
#define MAX_N_SETS 256
#define MIN_ASSOCIATIVITY 2
#define MAX_ASSOCIATIVITY 16

// Benchmark parameters
#define ARR_LENGTH (uint64_t)4 * GIGABYTE
#define N_ACCESSES 10000000

// Precision parameters
#define PRECISION 0.1
#define REQUIRED_N_CONVERGED_RUNS 5
#define TOTAL_RUNS_THRESHOLD 200

struct BenchmarkResult {
  int stride;
  double result;
  double increase;
};

bool benchmark_result_increase_comparator(BenchmarkResult a,
                                          BenchmarkResult b) {
  return a.increase < b.increase;
}

volatile uint8_t *allocate_array() {
  long page_size = sysconf(_SC_PAGE_SIZE);
  void *arr = aligned_alloc(page_size, ARR_LENGTH);
  if (arr == nullptr) {
    std::cout << "Failed to allocate array of length " << ARR_LENGTH
              << std::endl;
    std::exit(1);
  }
  return (uint8_t *)arr;
}

// void clear_array(volatile uint8_t *arr) { std::fill_n(arr, ARR_LENGTH, 0); }

int generate_chain(volatile uint8_t *arr, int stride) {
  // clear_array(arr);

  volatile uint64_t *ptr_arr = (volatile uint64_t *)arr;
  auto ptr_arr_size = ARR_LENGTH / sizeof(uint64_t);
  stride = stride / sizeof(uint64_t);

  int prev_index = 0;
  for (int index = stride; index < ptr_arr_size; index += stride) {
    ptr_arr[prev_index] = (uint64_t)&ptr_arr[index];
    prev_index = index;
  }
  ptr_arr[prev_index] = (uint64_t)&ptr_arr[0];
  return ptr_arr_size / stride;
}

long long benchmark(volatile uint8_t *arr) {
  auto value = (volatile uint64_t *)arr;
  auto start = std::chrono::steady_clock::now();
  // >>> begin benchmark
  for (uint64_t i = 0; i < N_ACCESSES; i++) {
    value = (volatile uint64_t *)*value;
  }
  // <<< end benchmark
  auto end = std::chrono::steady_clock::now();
  auto elapsed_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  std::cerr << "benchmark acc=" << (uint64_t)value << std::endl;
  return elapsed_ns;
}

double run_benchmark_until_converges(volatile uint8_t *arr) {
  int n = 0;
  long long sum = 0;
  double mean = 0;
  int n_successes = 0;
  while (n < TOTAL_RUNS_THRESHOLD) {
    auto bench_result = benchmark(arr);
    n++;
    sum += bench_result;
    auto cur_mean = ((double)sum) / n;
    auto current_err = abs(cur_mean - mean) / mean * 100;
    std::cerr << "Run " << n << ": Current benchmark results = " << cur_mean
              << ", current error = " << current_err << "%" << std::endl;
    if (current_err < PRECISION) {
      n_successes++;
      if (n_successes >= REQUIRED_N_CONVERGED_RUNS) {
        std::cerr << "Converged to " << cur_mean << " on the " << n
                  << "-th iteration" << std::endl;
        return cur_mean;
      }
    } else {
      n_successes = 0;
    }
    mean = cur_mean;
  }
  std::cout << "Benchmark results diverge!" << std::endl;
  std::exit(1);
}

std::vector<BenchmarkResult> run_benchmarks(volatile uint8_t *arr,
                                            int min_stride, int max_stride) {
  std::vector<BenchmarkResult> results;
  double prev_result = 1.0;
  for (int stride = min_stride; stride <= max_stride; stride *= 2) {
    BenchmarkResult benchmark_result;
    std::cerr << "\nStride = " << stride << std::endl;
    generate_chain(arr, stride);
    double current_result = run_benchmark_until_converges(arr);
    benchmark_result.stride = stride;
    benchmark_result.result = current_result;
    benchmark_result.increase =
        ((double)current_result) / ((double)prev_result);
    results.push_back(benchmark_result);

    std::cout << stride << ",\t" << current_result << ",\t"
              << benchmark_result.increase << std::endl;
    prev_result = current_result;
  }

  return results;
}

int find_cache_line(volatile uint8_t *arr) {
  auto results = run_benchmarks(arr, MIN_CACHELINE_SIZE, MAX_CACHELINE_SIZE);
  auto result_with_max_increase = std::max_element(
      results.begin() + 1, results.end(), benchmark_result_increase_comparator);
  return result_with_max_increase->stride;
}

int find_n_sets(volatile uint8_t *arr, int cache_line_size) {
  auto results =
      run_benchmarks(arr, MIN_N_SETS * cache_line_size, MAX_N_SETS * cache_line_size);
  auto result_with_max_increase = std::max_element(
      results.begin() + 1, results.end(), benchmark_result_increase_comparator);
  return result_with_max_increase->stride / cache_line_size;
}

int find_associativity(volatile uint8_t *arr, int cache_line_size) {
  auto results = run_benchmarks(arr, MIN_ASSOCIATIVITY * cache_line_size,
                                MAX_ASSOCIATIVITY * cache_line_size);
  auto result_with_min_increase = std::min_element(
      results.begin() + 1, results.end(), benchmark_result_increase_comparator);
  return result_with_min_increase->stride / cache_line_size;
}


int main() {
  auto arr = allocate_array();

  std::cout << "stride,\tresult,\tincrease" << std::endl;

  auto cache_line_size = find_cache_line(arr);
  auto n_sets = find_n_sets(arr, cache_line_size);
  // auto associativity = find_associativity(arr, cache_line_size);
  // auto set_size = cache_line_size * associativity;
  // auto cache_size = n_sets * set_size;

  std::cerr << std::endl
            << "Cache line size " << cache_line_size << std::endl
            << "Number of sets " << n_sets << std::endl;
            // << "Associativity " << associativity << std::endl
            // << "Size of each set " << set_size << std::endl
            // << "Cache size " << cache_size << std::endl;
  return 0;
}
