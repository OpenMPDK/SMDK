#include "smdk_opt_api.hpp"

#include "pnmlib/imdb/scan.h"
#include "pnmlib/imdb/scan_types.h"

#include "pnmlib/sls/embedding_tables.h"
#include "pnmlib/sls/operation.h"

#include "pnmlib/core/buffer.h"
#include "pnmlib/core/context.h"
#include "pnmlib/core/device.h"
#include "pnmlib/core/runner.h"

#include "pnmlib/common/views.h"

#include <type_traits>
#include <utility>

pnm::imdb::BitVectors SmdkAllocator::scan_range_bv(
                        const pnm::imdb::compressed_vector &column,
                        const pnm::imdb::Ranges &ranges){
    auto context = pnm::make_context(pnm::Device::Type::IMDB);
    const pnm::Runner runner(context);
    const pnm::memory::Buffer<uint32_t> column_buf(
        pnm::views::make_view(column.container()), context);
    pnm::imdb::BitVectors results;

    for (const auto &range : ranges) {
        pnm::imdb::bit_vector result(column.size() + 1);
        pnm::memory::Buffer<uint32_t> result_buf(
            pnm::views::make_view(result.container()), context);

        pnm::operations::Scan op(
            pnm::operations::Scan::Column{column.value_bits(), column.size(),
                                          &column_buf},
            range, &result_buf, pnm::imdb::OutputType::BitVector);

        runner.run(op);

        result_buf.copy_from_device();
        results.push_back(std::move(result));
    }
    return results;
}

pnm::imdb::IndexVectors SmdkAllocator::scan_range_iv(
                        const pnm::imdb::compressed_vector &column,
                        const pnm::imdb::Ranges &ranges){
    auto context = pnm::make_context(pnm::Device::Type::IMDB);
    const pnm::Runner runner(context);
    const pnm::memory::Buffer<uint32_t> column_buf(
        pnm::views::make_view(column.container()), context);
    pnm::imdb::IndexVectors results;

    for (const auto &range : ranges) {
        pnm::imdb::index_vector result(column.size() + 1);
        pnm::memory::Buffer<uint32_t> result_buf(
            pnm::views::make_view(result), context);

        pnm::operations::Scan op(
            pnm::operations::Scan::Column{column.value_bits(), column.size(),
                                          &column_buf},
            range, &result_buf, pnm::imdb::OutputType::IndexVector);

        runner.run(op);

        result_buf.copy_from_device();
        result.resize(op.result_size());
        results.push_back(std::move(result));
    }
    return results;
}

pnm::imdb::BitVectors SmdkAllocator::scan_list_bv(
                        const pnm::imdb::compressed_vector &column,
                        const pnm::imdb::Predictors &predictors){
    auto context = pnm::make_context(pnm::Device::Type::IMDB);
    const pnm::Runner runner(context);
    const pnm::memory::Buffer<uint32_t> column_buf(
        pnm::views::make_view(column.container()), context);
    pnm::imdb::BitVectors results;

    for (const auto &predictor : predictors) {
        pnm::imdb::bit_vector result(column.size() + 1);
        pnm::memory::Buffer<uint32_t> result_buf(
            pnm::views::make_view(result.container()), context);
        pnm::memory::Buffer<uint32_t> predictor_buf(
            pnm::views::make_view(predictor.container()), context);

        pnm::operations::Scan op(
            pnm::operations::Scan::Column{column.value_bits(), column.size(),
                                          &column_buf},
            pnm::operations::Scan::InListPredicate{predictor.size(),
                                                   &predictor_buf},
            &result_buf, pnm::imdb::OutputType::BitVector);

        runner.run(op);

        result_buf.copy_from_device();
        results.push_back(std::move(result));
    }
    return results;
}

pnm::imdb::IndexVectors SmdkAllocator::scan_list_iv(
                        const pnm::imdb::compressed_vector &column,
                        const pnm::imdb::Predictors &predictors){
    auto context = pnm::make_context(pnm::Device::Type::IMDB);
    const pnm::Runner runner(context);
    const pnm::memory::Buffer<uint32_t> column_buf(
        pnm::views::make_view(column.container()), context);
    pnm::imdb::IndexVectors results;

    for (const auto &predictor : predictors) {
        pnm::imdb::index_vector result(column.size() + 1);
        pnm::memory::Buffer<uint32_t> result_buf(
            pnm::views::make_view(result), context);
        pnm::memory::Buffer<uint32_t> predictor_buf(
            pnm::views::make_view(predictor.container()), context);

        pnm::operations::Scan op(
            pnm::operations::Scan::Column{column.value_bits(), column.size(),
                                          &column_buf},
            pnm::operations::Scan::InListPredicate{predictor.size(),
                                                   &predictor_buf},
            &result_buf, pnm::imdb::OutputType::IndexVector);

        runner.run(op);

        result_buf.copy_from_device();
        result.resize(op.result_size());
        results.push_back(std::move(result));
    }
    return results;
}
