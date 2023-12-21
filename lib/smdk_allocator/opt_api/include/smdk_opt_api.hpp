#ifndef SMDK_OPT_API_HPP_
#define SMDK_OPT_API_HPP_

#include <assert.h>
#include <string.h>
#include "smdk_opt_api.h"

#include "pnmlib/imdb/scan.h"
#include "pnmlib/imdb/scan_types.h"

#include "pnmlib/sls/operation.h"

#include "pnmlib/core/buffer.h"
#include "pnmlib/core/context.h"
#include "pnmlib/core/device.h"
#include "pnmlib/core/runner.h"

#include "pnmlib/common/views.h"

#include <linux/sls_resources.h>

#include <type_traits>
#include <utility>

#define IS_PROCESS_ARGS(Dev, Type, Op)             \
    if(dev == SmdkAllocator::Device::Dev &&        \
       pnmtype == SmdkAllocator::PNMType::Type &&  \
       op == SmdkAllocator::Operation::Op)

using std::string;

template <typename T>
struct SlsTable
{
    const std::vector<T> &tables;
    uint32_t tables_count;
    uint32_t rows_count;
    uint32_t feature_size;
    sls_user_preferences alloc_option = SLS_ALLOC_AUTO;
    
    pnm::operations::SlsOperation::Type data_type() const;
};

template <>
pnm::operations::SlsOperation::Type SlsTable<float>::data_type() const
{
    return pnm::operations::SlsOperation::Type::Float;
}

template<>
pnm::operations::SlsOperation::Type SlsTable<uint32_t>::data_type() const
{
    return pnm::operations::SlsOperation::Type::Uint32;
}

struct SlsParam
{
    uint32_t n_batch;
    const std::vector<uint32_t> &lengths;
    const std::vector<uint32_t> &indices;
};

class SmdkAllocator
{
public:
    enum class Device : uint8_t { DDR, MXP, PNM };
    enum class PNMType : uint8_t { IMDB, DLRM };
    enum class Operation : uint8_t { ScanRange, ScanList, Sls };
    static SmdkAllocator& get_instance()
    {
        static SmdkAllocator s;
        return s;
    }
    void *malloc(smdk_memtype_t type, size_t size)
    {
        return s_malloc(type, size);
    }
    void *calloc(smdk_memtype_t type, size_t num, size_t size)
    {
        return s_calloc(type, num, size);
    }
    void *realloc(smdk_memtype_t type, void *ptr, size_t size)
    {
        return s_realloc(type, ptr, size);
    }
    int posix_memalign(smdk_memtype_t type, void **memptr, size_t alignment, size_t size)
    {
        return s_posix_memalign(type, memptr, alignment, size);
    }
    void free(void *ptr)
    {
        return s_free(ptr);
    }
    void free(smdk_memtype_t type, void *ptr)
    {
        return s_free_type(type, ptr);
    }
    size_t get_memsize_total(smdk_memtype_t type)
    {
        return s_get_memsize_total(type);
    }
    size_t get_memsize_used(smdk_memtype_t type)
    {
        return s_get_memsize_used(type);
    }
    size_t get_memsize_available(smdk_memtype_t type)
    {
        return s_get_memsize_available(type);
    }
    void stats_print(char unit)
    {
        s_stats_print(unit);
    }
    void stats_node_print(char unit)
    {
        s_stats_node_print(unit);
    }
    void enable_node_interleave(string nodes)
    {
        s_enable_node_interleave((char *)nodes.c_str());
    }
    void disable_node_interleave(void)
    {
        s_disable_node_interleave();
    }
    void *malloc_node(smdk_memtype_t type, size_t size, string node)
    {
        return s_malloc_node(type, size, (char *)node.c_str());
    }
    void free_node(smdk_memtype_t type, void *mem, size_t size)
    {
        return s_free_node(type, mem, size);
    }

    void process(Device dev, PNMType pnmtype, Operation op,
                 const pnm::imdb::compressed_vector &column,
                 const pnm::imdb::Ranges &ranges,
                 pnm::imdb::BitVectors &results)
    {
        IS_PROCESS_ARGS(PNM, IMDB, ScanRange)
            results = scan_range_bv(column, ranges);
    }
    void process(Device dev, PNMType pnmtype, Operation op,
                 const pnm::imdb::compressed_vector &column,
                 const pnm::imdb::Ranges &ranges,
                 pnm::imdb::IndexVectors &results)
    {
        IS_PROCESS_ARGS(PNM, IMDB, ScanRange)
            results = scan_range_iv(column, ranges);
    }
    void process(Device dev, PNMType pnmtype, Operation op,
                 const pnm::imdb::compressed_vector &column,
                 const pnm::imdb::Predictors &predictors,
                 pnm::imdb::BitVectors &results)
    {
        IS_PROCESS_ARGS(PNM, IMDB, ScanList)
            results = scan_list_bv(column, predictors);
    }
    void process(Device dev, PNMType pnmtype, Operation op,
                 const pnm::imdb::compressed_vector &column,
                 const pnm::imdb::Predictors &predictors,
                 pnm::imdb::IndexVectors &results)
    {
        IS_PROCESS_ARGS(PNM, IMDB, ScanList)
            results = scan_list_iv(column, predictors);
    }
    template <typename T>
    void process(Device dev, PNMType pnmtype, Operation op,
                 const SlsTable<T> &table, const SlsParam &op_param,
                 std::vector<T> &results)
    {
        IS_PROCESS_ARGS(PNM, DLRM, Sls)
            results = sls<T>(table, op_param);
    }
private:
    SmdkAllocator() = default;
    SmdkAllocator(const SmdkAllocator& ref) = delete;
    SmdkAllocator& operator=(const SmdkAllocator& ref) = delete;
    ~SmdkAllocator() = default;
    
    pnm::imdb::BitVectors scan_range_bv(
        const pnm::imdb::compressed_vector &column,
        const pnm::imdb::Ranges &ranges);
    pnm::imdb::IndexVectors scan_range_iv(
        const pnm::imdb::compressed_vector &column,
        const pnm::imdb::Ranges &ranges);
    pnm::imdb::BitVectors scan_list_bv(
        const pnm::imdb::compressed_vector &column,
        const pnm::imdb::Predictors &predictors);
    pnm::imdb::IndexVectors scan_list_iv(
        const pnm::imdb::compressed_vector &column,
        const pnm::imdb::Predictors &predictors);
    template <typename T>
    std::vector<T> sls(const SlsTable<T> &table, const SlsParam &op_param)
    {
        auto context = pnm::make_context(pnm::Device::Type::SLS);
        const pnm::Runner runner(context);

        const std::vector<uint32_t> TSizes(table.tables_count,
                                           table.rows_count);

        auto embedding_tables = pnm::memory::EmbeddingTables::create(
            pnm::views::make_const_view(table.tables), TSizes,
            table.feature_size * sizeof(T), context, table.alloc_option);

        std::vector<T> psum(
            table.tables_count * op_param.n_batch * table.feature_size, 0);

        pnm::operations::SlsOperation op(
            table.feature_size, pnm::views::make_view(TSizes),
            embedding_tables.get(), table.data_type());

        op.set_run_params(
            op_param.n_batch, pnm::views::make_const_view(op_param.lengths),
            pnm::views::make_const_view(op_param.indices),
            pnm::views::view_cast<uint8_t>(pnm::views::make_view(psum)));

        runner.run(op);

        return psum;
    }
};
#endif /* SMDK_OPT_API_HPP_ */
