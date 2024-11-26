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

// --- General definitions
#define KILOBYTE 1024
#define MEGABYTE 1024 * KILOBYTE
#define GIGABYTE 1024 * MEGABYTE

// --- Search bounds
// Cache line size
#define MIN_CACHELINE_SIZE 16
#define MAX_CACHELINE_SIZE 128
// Cache size
#define MIN_CACHESIZE 32 * KILOBYTE
#define MAX_CACHESIZE 70 * KILOBYTE
#define CACHESIZE_STEP 2 * KILOBYTE
// Number of sets
#define MIN_N_SETS 8
#define MAX_N_SETS 128

// Statistical thresholds
#define CACHESIZE_JUMP_THRESHOLD 1e7
#define N_SETS_JUMP_THRESHOLD (2 * 1e8)
#define N_SETS_STABILIZATION_EPSILON (1e8)

// Benchmark parameters
#define ARR_LENGTH (uint64_t)4 * GIGABYTE
#define N_ACCESSES 500000000

// Precision parameters
#define PRECISION 1
#define REQUIRED_N_CONVERGED_RUNS 5
#define TOTAL_RUNS_THRESHOLD 200

struct BenchmarkParameters {
  int stride;
  uint64_t arr_size;
};

struct BenchmarkResult {
  BenchmarkParameters parameters;
  double result;
  double increase;
};

int find_first_performance_spike(std::vector<BenchmarkResult> const &results) {
  // Compute mean increase
  double sum = 0;
  for (auto it = results.begin() + 1; it != results.end(); ++it) {
    sum += it->increase;
  }
  double mean_increase = sum / (results.size() - 1);

  // Find first stride for which increase is greater than mean
  for (auto it = results.begin() + 1; it != results.end(); ++it) {
    if (it->increase > mean_increase) {
      return it->parameters.stride;
    }
  }
  return -1;
}

volatile uint8_t *allocate_array() {
  long page_size = sysconf(_SC_PAGE_SIZE);
  void *arr = aligned_alloc(page_size, ARR_LENGTH);
  std::cerr << "Allocated array of " << ARR_LENGTH << " bytes" << std::endl;
  if (arr == nullptr) {
    std::cerr << "Failed to allocate array of length " << ARR_LENGTH
              << std::endl;
    std::exit(1);
  }
  std::fill_n((volatile uint8_t *)arr, ARR_LENGTH, (uint8_t)0);
  return (uint8_t *)arr;
}

int generate_chain(volatile uint8_t *arr, int stride, uint64_t arr_size) {
  volatile uint64_t *ptr_arr = (volatile uint64_t *)arr;
  auto ptr_arr_size = arr_size / sizeof(uint64_t);
  stride = stride / sizeof(uint64_t);

  uint64_t prev_index = 0;
  for (uint64_t index = stride; index < ptr_arr_size; index += stride) {
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

std::vector<BenchmarkResult>
run_benchmarks(volatile uint8_t *arr,
               std::vector<BenchmarkParameters> const &parameters_sequence) {
  std::vector<BenchmarkResult> results;
  double prev_result = 1.0;
  for (BenchmarkParameters param : parameters_sequence) {
    BenchmarkResult benchmark_result;
    std::cerr << "\nStride = " << param.stride
              << ", array size = " << param.arr_size << std::endl;
    generate_chain(arr, param.stride, param.arr_size);
    double current_result = run_benchmark_until_converges(arr);
    benchmark_result.parameters = param;
    benchmark_result.result = current_result;
    benchmark_result.increase =
        ((double)current_result) / ((double)prev_result);
    results.push_back(benchmark_result);

    std::cout << param.stride << "," << param.arr_size << "," << current_result
              << "," << benchmark_result.increase << std::endl;
    prev_result = current_result;
  }

  return results;
}

std::vector<BenchmarkParameters>
get_strides_parameters_sequence(int min_stride, int max_stride,
                                uint64_t fixed_arr_size) {
  std::vector<BenchmarkParameters> parameters_sequence;
  for (int stride = min_stride; stride <= max_stride; stride *= 2) {
    BenchmarkParameters params;
    params.stride = stride;
    params.arr_size = fixed_arr_size;
    parameters_sequence.push_back(params);
  }
  return parameters_sequence;
}

int find_cache_line(volatile uint8_t *arr) {
  auto params = get_strides_parameters_sequence(MIN_CACHELINE_SIZE,
                                                MAX_CACHELINE_SIZE, ARR_LENGTH);
  auto results = run_benchmarks(arr, params);
  auto result_stride = find_first_performance_spike(results);
  if (result_stride != -1) {
    return result_stride;
  } else {
    std::cerr
        << "Could not detect cache line size: no performance spikes detected!"
        << std::endl;
    std::exit(1);
  }
}

uint64_t find_cache_size(volatile uint8_t *arr, int cache_line_size) {
  int stride = 2 * cache_line_size;
  // 1. Form a sequence
  std::vector<BenchmarkParameters> parameters_sequence;
  for (int arr_length = MIN_CACHESIZE; arr_length <= MAX_CACHESIZE;
       arr_length += CACHESIZE_STEP) {
    BenchmarkParameters params;
    params.stride = stride;
    params.arr_size = arr_length;
    parameters_sequence.push_back(params);
  }
  // 2. Run experiments
  auto results = run_benchmarks(arr, parameters_sequence);
  // 3. Analyze results
  double prev_result = results[0].result;
  for (size_t i = 1; i < results.size(); i++) {
    auto diff = results[i].result - prev_result;
    if (diff >= CACHESIZE_JUMP_THRESHOLD) {
      return results[i].parameters.arr_size;
    }
  }
  std::cerr << "Could not detect cache size!" << std::endl;
  std::exit(1);
}

int find_associativity(volatile uint8_t *arr, int cache_line_size,
                       uint64_t cache_size) {
  int stride = cache_line_size * MAX_N_SETS;
  // 1. Form a sequence
  std::vector<BenchmarkParameters> parameters_sequence;
  for (uint64_t assumed_associativity = 4; assumed_associativity <= 16;
       assumed_associativity += 2) {
    BenchmarkParameters params = {.stride = stride,
                                  .arr_size = assumed_associativity * stride};
    parameters_sequence.push_back(params);
  }
  // 2. Run experiments
  auto results = run_benchmarks(arr, parameters_sequence);
  // 3. Analyze results
  double prev_result = results[0].result;
  for (size_t i = 1; i < results.size(); i++) {
    auto diff = results[i].result - prev_result;
    if (diff >= 1.5 * 1e8) {
      uint64_t assumed_associativity = results[i].parameters.arr_size / stride;
      uint64_t assumed_n_sets =
          cache_size / (assumed_associativity * cache_line_size);
      uint64_t rounded_n_sets = 1 << (std::bit_width(assumed_n_sets) - 1);
      uint64_t rounded_associativity =
          (cache_size / rounded_n_sets) / cache_line_size;
      return rounded_associativity;
    }
  }
  std::cerr << "Could not detect associativity!" << std::endl;
  std::exit(1);
}

int main() {
  auto arr = allocate_array();

  std::cout << "stride,arr_size,result,increase" << std::endl;

  // 49152
  int cache_line_size = find_cache_line(arr);
  std::cerr << "Result: cache line size is " << cache_line_size << std::endl;

  uint64_t cache_size = find_cache_size(arr, cache_line_size);
  std::cerr << "Result: cache size is " << cache_size << std::endl;

  int associativity = find_associativity(arr, 64, 49152);
  std::cerr << "Result: associativity is " << associativity << std::endl;

  std::cerr << std::endl
            << "Cache line size: " << cache_line_size << std::endl
            << "Cache size:      " << cache_size << std::endl
            << "Associativity:   " << associativity << std::endl;
  return 0;
}
