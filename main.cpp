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
#define ARR_LENGTH (uint64_t)16 * GIGABYTE
#define N_ACCESSES 10000000

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
  std::fill_n((volatile uint8_t *)arr, ARR_LENGTH, (uint8_t)0);
  return (uint8_t *)arr;
}

int generate_chain(volatile uint8_t *arr, int stride, uint64_t arr_size) {
  volatile uint64_t *ptr_arr = (volatile uint64_t *)arr;
  auto ptr_arr_size = arr_size / sizeof(uint64_t);
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

std::vector<BenchmarkParameters>
get_arr_sizes_parameters_sequence(uint64_t min_arr_size, uint64_t max_arr_size,
                                  uint64_t step, int fixed_stride) {
  std::vector<BenchmarkParameters> parameters_sequence;
  for (int arr_size = min_arr_size; arr_size <= max_arr_size;
       arr_size += step) {
    BenchmarkParameters params;
    params.stride = fixed_stride;
    params.arr_size = arr_size;
    parameters_sequence.push_back(params);
  }
  return parameters_sequence;
}

int find_cache_line(volatile uint8_t *arr) {
  auto params = get_strides_parameters_sequence(MIN_CACHELINE_SIZE,
                                                MAX_CACHELINE_SIZE, ARR_LENGTH);
  auto results = run_benchmarks(arr, params);
  auto result_with_max_increase = std::max_element(
      results.begin() + 1, results.end(), benchmark_result_increase_comparator);
  return result_with_max_increase->parameters.stride;
}

int find_n_sets(volatile uint8_t *arr, int cache_line_size) {
  auto params = get_strides_parameters_sequence(
      MIN_N_SETS * cache_line_size, MAX_N_SETS * cache_line_size, ARR_LENGTH);
  auto results = run_benchmarks(arr, params);
  auto result_with_max_increase = std::max_element(
      results.begin() + 1, results.end(), benchmark_result_increase_comparator);
  return result_with_max_increase->parameters.stride / cache_line_size;
}

int find_associativity(volatile uint8_t *arr, int cache_line_size, int n_sets) {
  // Size of cache if it was 1-way associative
  uint64_t one_way_associative_cache_size = cache_line_size * n_sets;
  auto params = get_arr_sizes_parameters_sequence(
      MIN_ASSOCIATIVITY * one_way_associative_cache_size,
      MAX_ASSOCIATIVITY * one_way_associative_cache_size,
      2 * one_way_associative_cache_size, cache_line_size);
  auto results = run_benchmarks(arr, params);
  auto result_with_max_increase = std::max_element(
      results.begin() + 1, results.end(), benchmark_result_increase_comparator);
  return result_with_max_increase->parameters.arr_size /
         one_way_associative_cache_size;
}

int main() {
  auto arr = allocate_array();

  std::cout << "stride,arr_size,result,increase" << std::endl;

  auto cache_line_size = find_cache_line(arr);
  auto n_sets = find_n_sets(arr, cache_line_size);
  auto associativity = find_associativity(arr, cache_line_size, n_sets);
  auto size_of_each_set = cache_line_size * associativity;
  auto cache_size = ((uint64_t)size_of_each_set) * n_sets;

  std::cerr << std::endl
            << "Cache line size " << cache_line_size << std::endl
            << "Number of sets " << n_sets << std::endl
            << "Associativity " << associativity << std::endl
            << "Size of each set " << size_of_each_set << std::endl
            << "Cache size " << cache_size << std::endl;
  // << "Associativity " << associativity << std::endl
  // << "Size of each set " << set_size << std::endl
  // << "Cache size " << cache_size << std::endl;
  return 0;
}
