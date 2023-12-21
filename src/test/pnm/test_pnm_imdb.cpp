#include "smdk_opt_api.hpp"

#include "tools/datagen/imdb/column_generator/factory.h"
#include "tools/datagen/imdb/predicate_vec_generator/factory.h"
#include "tools/datagen/imdb/range_generator/factory.h"
#include "tools/datagen/imdb/utils.h"

#include "pnmlib/imdb/scan_types.h"

#include <fmt/core.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <future>
#include <ratio>
#include <utility>
#include <vector>

namespace {

std::filesystem::path data_root = PATH_TO_TABLES;

inline constexpr auto column_gen_names =
    {tools::gen::imdb::literals::column_random_generator,
     tools::gen::imdb::literals::column_positioned_generator};
inline constexpr auto ranges_gen_name =
    tools::gen::imdb::literals::scan_ranges_generator_random;
inline constexpr auto pred_gen_name =
    tools::gen::imdb::literals::predictor_generator_random;
inline constexpr auto bit_compressions = {2, 5, 6, 7, 9, 10, 12, 16, 17, 18};
inline constexpr auto column_size = 10000;
inline constexpr auto num_scan = 100;
inline constexpr auto selectivity = 0.5;

inline auto num_test = 1;
inline auto test_failed = 0;

} // namespace

template <typename op_type, typename vector_type>
uint64_t subtest(const pnm::imdb::compressed_vector &column,
                 const op_type &operations, const vector_type &golden_results) {
  vector_type results;

  SmdkAllocator& allocator = SmdkAllocator::get_instance();
  allocator.process(SmdkAllocator::Device::PNM,
                    SmdkAllocator::PNMType::IMDB,
                    (std::is_same<op_type, pnm::imdb::Ranges>::value ?
                      SmdkAllocator::Operation::ScanRange :
                      SmdkAllocator::Operation::ScanList),
                    column, operations, results);

  if (!std::equal(golden_results.begin(), golden_results.end(), results.begin(),
      results.end())) {
    fmt::print("Operation result does not equal to the golden result\n");
    return 1;
  }
  return 0;
}

template <pnm::imdb::OutputType scan_output_type>
void test_RangeScan(size_t num_threads, std::string column_gen_name,
                    uint64_t bit_compression) {
  bool sub_test_failed = false;

  // print Test Param Info
  fmt::print("Sub Test starts\n");
  fmt::print("  [Test Info] Column Generator: {}, Bit Compression: {}\n",
             column_gen_name, bit_compression);

  // generate table, ranges and golden vectors
  const auto[column, input, golden] =
    tools::gen::imdb::generate_and_store_data<
      pnm::imdb::OperationType::InRange, scan_output_type>(
        ::data_root, column_gen_name, ::ranges_gen_name, bit_compression,
        ::column_size, ::num_scan, ::selectivity, true);

  // create N workers = threads
  std::vector<std::future<uint64_t>> workers(num_threads);

  for (auto &worker : workers) {
    if constexpr (scan_output_type == pnm::imdb::OutputType::BitVector) {
      worker = std::async(std::launch::async,
                          subtest<pnm::imdb::Ranges, pnm::imdb::BitVectors>,
                          std::cref(column), std::cref(input),
                          std::cref(golden));
    }
    else {
      worker = std::async(std::launch::async,
                          subtest<pnm::imdb::Ranges, pnm::imdb::IndexVectors>,
                          std::cref(column), std::cref(input),
                          std::cref(golden));
    }
  }

  // wait for all workers to complete
  for (size_t i = 0; i < num_threads; ++i) {
    auto ret = workers[i].get();
    if (ret != 0) {
      fmt::print(" #{} worker failed\n", i + 1);
      sub_test_failed = true;
    }
  }

  if (sub_test_failed) {
    fmt::print("Sub Test done - FAIL\n");
    ::test_failed++;
  }
  else {
    fmt::print("Sub Test done - PASS\n");
  }
}

template <pnm::imdb::OutputType scan_output_type>
void test_ListScan(size_t num_threads, std::string column_gen_name,
                   uint64_t bit_compression) {
  bool sub_test_failed = false;

  // print Test Param Info
  fmt::print("Sub Test starts\n");
  fmt::print("  [Test Info] Column Generator: {}, Bit Compression: {}\n",
             column_gen_name, bit_compression);

  // generate table, ranges and golden vectors
  const auto[column, input, golden] =
    tools::gen::imdb::generate_and_store_data<
      pnm::imdb::OperationType::InList, scan_output_type>(
        ::data_root, column_gen_name, ::pred_gen_name, bit_compression,
        ::column_size, ::num_scan, ::selectivity, true);

  // create N workers = threads
  std::vector<std::future<uint64_t>> workers(num_threads);

  for (auto &worker : workers) {
    if constexpr (scan_output_type == pnm::imdb::OutputType::BitVector) {
      worker = std::async(std::launch::async,
                          subtest<pnm::imdb::Predictors, pnm::imdb::BitVectors>,
                          std::cref(column), std::cref(input),
                          std::cref(golden));
    }
    else {
      worker = std::async(std::launch::async,
                          subtest<pnm::imdb::Predictors, pnm::imdb::IndexVectors>,
                          std::cref(column), std::cref(input),
                          std::cref(golden));
    }
  }

  // wait for all workers to complete
  for (size_t i = 0; i < num_threads; ++i) {
    auto ret = workers[i].get();
    if (ret != 0) {
      fmt::print(" #{} worker failed\n", i + 1);
      sub_test_failed = true;
    }
  }

  if (sub_test_failed) {
    fmt::print("Sub Test done - FAIL\n");
    ::test_failed++;
  }
  else {
    fmt::print("Sub Test done - PASS\n");
  }
}

void test(std::string op_type, std::string vector_type) {
  auto num_thread = 1;
 
  fmt::print("[Test #{}] {} - Output {} starts\n", ::num_test, op_type, vector_type);

  for (const auto &column_gen_name : column_gen_names) {
    for (const auto &bit_compression : bit_compressions) {
      if (op_type =="RangeScan") {
        if (vector_type == "IV")
          test_RangeScan<pnm::imdb::OutputType::IndexVector>(num_thread, column_gen_name, bit_compression);
        else
          test_RangeScan<pnm::imdb::OutputType::BitVector>(num_thread, column_gen_name, bit_compression);
      }
      else {
        if (vector_type == "IV") 
          test_ListScan<pnm::imdb::OutputType::IndexVector>(num_thread, column_gen_name, bit_compression);
        else 
          test_ListScan<pnm::imdb::OutputType::BitVector>(num_thread, column_gen_name, bit_compression);
      }
    }
  }

  fmt::print("[Test #{}] {} - Output {} done\n\n", ::num_test++, op_type, vector_type);
}

int main(){
  test("RangeScan", "BV");
  test("RangeScan", "IV");
  test("ListScan", "BV");
  test("ListScan", "IV");

  if(::test_failed)
    return 1;
  else
    return 0;
}
