#include "smdk_opt_api.hpp"

#include "tools/datagen/sls/general/traits.h"
#include "tools/datagen/sls/utils.h"

#include <fmt/core.h>
#include <fmt/format.h>

#include <linux/sls_resources.h>

#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iosfwd>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

namespace {

std::filesystem::path data_root = PATH_TO_TABLES;

inline const std::vector<uint32_t> tables_counts = {50, 6};
inline uint32_t rows_number = 500000;
inline constexpr auto sparse_feature_sizes = {16, 32};
inline constexpr auto user_preferences = {SLS_ALLOC_AUTO,
                                          SLS_ALLOC_DISTRIBUTE_ALL};
inline const std::vector<uint32_t> batch_sizes = {16, 128};
inline const std::vector<uint32_t> max_lookup = {500, 40};
inline const std::vector<uint32_t> min_lookup = {1, 40};
inline constexpr auto num_idx_values = 32;

inline auto num_test = 1;
inline auto test_failed = 0;

} // namespace

template <typename T>
std::vector<T> make_embedding_table(uint32_t tables_count, uint32_t rows_number,
                                    uint32_t sparse_feature_size,
                                    const std::vector<uint8_t> &table_in) {
  std::vector<T> etable(tables_count * rows_number * sparse_feature_size);
  auto *data_in = reinterpret_cast<const T *>(table_in.data());

  for (auto i = 0UL; i < tables_count; i++) {
    for (auto j = 0UL; j < rows_number; j++) {
      for (auto k = 0UL; k < sparse_feature_size; k++) {
        auto idx = (i * rows_number + j) * sparse_feature_size + k;
        etable[idx] = *(data_in + idx);
      }
    }
  }
  return etable;
}

template <typename T>
void test(uint32_t tables_count, uint32_t batch_size,
          uint32_t sparse_feature_size, sls_user_preferences user_preference,
          uint32_t max_lookup, uint32_t min_lookup){
  std::string type_name = tools::gen::sls::DataTypeTraits<T>().name;
  std::string alloc_option = (user_preference == SLS_ALLOC_AUTO ?
                                                 "REPLICATE_ALL" :
                                                 "DISTRIBUTE_ALL");

  fmt::print("Sub Test starts\n");
  fmt::print("  [Table Info] Tables count: {}, Rows number: {}, Feature size: {}\n",
             tables_count, rows_number, sparse_feature_size);
  fmt::print("  [SLSOp Info] Batch: {}, Max_lookup: {}, Min_lookup: {}, Alloc_option: {}\n",
             batch_size, max_lookup, min_lookup, alloc_option);

  // Generate Embedding Tables
  auto data_name = fmt::format("dlrm_sls_{}_{}tables_{}rows_{}features",
                               type_name, tables_count, rows_number,
                               sparse_feature_size);
  std::filesystem::path data_path = data_root / data_name;
  std::string gen_name = type_name + "_tapp";
  std::string gen_arg = std::to_string(num_idx_values) + " " +
                        std::to_string(sparse_feature_size);

  std::vector<uint8_t> base_table;
  tools::gen::sls::get_or_create_test_tables(
    data_path, gen_name, gen_arg, sparse_feature_size, tables_count,
    rows_number, base_table);

  std::vector<T> etable = make_embedding_table<T>(
    tables_count, rows_number, sparse_feature_size, base_table);

  // Generate Lookup Indices for SLS Operation
  auto indices_name = fmt::format("min_{}_max_{}_batch_{}",
    min_lookup, max_lookup, batch_size);

  tools::gen::sls::GeneratorWithOverflowParams gen_params{
    .max_num_lookup = max_lookup,
    .min_num_lookup = min_lookup,
    .mini_batch_size = int(batch_size),
    .root = data_path,
    .prefix = indices_name,
    .generator_lengths_name = "random",
    .overflow_type = "withoutOverflow",
    .generator_indices_name = "random",
    .entry_type = type_name};

  std::vector<uint32_t> lengths;
  std::vector<uint32_t> indices;

  tools::gen::sls::get_or_create_test_indices(gen_params, lengths, indices);

  // Process SLS Operation
  std::vector<T> results(tables_count * batch_size * sparse_feature_size, 0);

  SlsTable<T> table_info = {etable, tables_count, rows_number,
                            sparse_feature_size, user_preference};
  SlsParam params = {batch_size, lengths, indices};

  SmdkAllocator& allocator = SmdkAllocator::get_instance();
  allocator.process(SmdkAllocator::Device::PNM,
                    SmdkAllocator::PNMType::DLRM,
                    SmdkAllocator::Operation::Sls,
                    table_info, params, results);

  // Check Process Result with Golden Result
  std::vector<T> golden_results;
  std::ifstream in_golden = tools::gen::sls::get_golden_vector(data_path, indices_name);

  if (in_golden.is_open())
  {
    std::streampos size = 0;
    size = in_golden.tellg();
    in_golden.seekg(0, std::ios::end);
    size = in_golden.tellg() - size;
    in_golden.seekg(0, std::ios::beg);

    golden_results.resize(size / sizeof(T));
    in_golden.read(reinterpret_cast<char *>(golden_results.data()), size);
  }
  else{
    fmt::print("Can not open the golden result.\n");
    fmt::print("Sub Test done - FAIL\n");
    ::test_failed++;
    return;
  }

  if (!std::equal(golden_results.begin(), golden_results.end(), results.begin(),
      results.end())) {
    fmt::print("Operation result does not equal to the golden result.\n");
    fmt::print("Sub Test done - FAIL\n");
    ::test_failed++;
    return;
  }
  fmt::print("Sub Test done - PASS\n");
}

void test_sls_float(){
  fmt::print("[Test #{}] SLS - Data Type: Float starts\n", ::num_test);

  for(int i = 0 ; i < 2; i++) { // tables_counts, batch_sizes
    for(int j = 0; j < 2; j++) { // max_lookup, min_lookup
      for(auto &sparse_feature_size : sparse_feature_sizes) {
        for(auto &user_preference : user_preferences){
          test<float>(tables_counts[i], batch_sizes[i],
                      sparse_feature_size, user_preference,
                      max_lookup[j], min_lookup[j]);
        }
      }
    }
  }

  fmt::print("[Test #{}] SLS - Data Type: Float done\n\n", ::num_test++);
}

void test_sls_int(){
  fmt::print("[Test #{}] SLS - Data Type: Uint32 starts\n", ::num_test);

   for(int i = 0 ; i < 2; i++) { // tables_counts, batch_sizes
     for(int j = 0; j < 2; j++) { // max_lookup, min_lookup
      for(auto &sparse_feature_size : sparse_feature_sizes) {
        for(auto &user_preference : user_preferences){
          test<uint32_t>(tables_counts[i], batch_sizes[i],
                         sparse_feature_size, user_preference,
                         max_lookup[j], min_lookup[j]);
        }
      }
     }
   }

  fmt::print("[Test #{}] SLS - Data Type: Uint32 done\n\n", ::num_test++);
}

int main(int argc, char* argv[]){
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "row"))
      rows_number = (uint32_t)atoi(argv[++i]);
  }

  test_sls_float();
  test_sls_int();
 
  if(::test_failed)
    return 1;
  else
    return 0;
}
