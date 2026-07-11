#include <projecta/concurrency/thread_pool.hpp>

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pc = projecta::concurrency;

static void SubmitAndWait(benchmark::State& state) {
    const auto workers = static_cast<std::size_t>(state.range(0));
    pc::ThreadPool pool{{.thread_count = workers, .queue_capacity = 4096}};
    for (auto _ : state) {
        auto submitted = pool.submit([] {});
        if (!submitted.has_value()) {
            state.SkipWithError("submission rejected");
            break;
        }
        std::move(submitted).value().get();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(SubmitAndWait)->Arg(1)->Arg(2)->Arg(4)->UseRealTime();

static void BatchThroughput(benchmark::State& state) {
    const auto workers = static_cast<std::size_t>(state.range(0));
    constexpr std::size_t batch_size = 1024;
    pc::ThreadPool pool{{.thread_count = workers, .queue_capacity = batch_size}};
    for (auto _ : state) {
        std::vector<pc::TaskHandle<void>> handles;
        handles.reserve(batch_size);
        for (std::size_t i = 0; i < batch_size; ++i) {
            auto submitted = pool.submit([] {});
            if (!submitted.has_value()) {
                state.SkipWithError("submission rejected");
                return;
            }
            handles.push_back(std::move(submitted).value());
        }
        for (auto& handle : handles) {
            handle.get();
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(batch_size));
}
BENCHMARK(BatchThroughput)->Arg(1)->Arg(2)->Arg(4)->UseRealTime();
