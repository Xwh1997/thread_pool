#include <projecta/concurrency/thread_pool.hpp>

int main() {
    projecta::concurrency::ThreadPool pool{{.thread_count = 2, .queue_capacity = 8}};
    auto result = pool.submit([] { return 42; });
    if (!result.has_value()) {
        return 1;
    }
    return std::move(result).value().get() == 42 ? 0 : 2;
}
