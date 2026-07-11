# ProjectA ThreadPool

一个面向服务端通用任务的 C++20 有界线程池。v2 将过载、关闭、取消和同池等待定义为明确契约，并以 `ProjectA::thread_pool` 形式提供可安装 CMake 包。

## 特性

- 固定 worker、中央有界 FIFO；容量只计算尚未开始的任务。
- `submit` 立即返回，队列满时报告 `queue_full`；`submit_for` 提供显式限时背压。
- move-only callable/参数与 typed `TaskHandle<R>`；任务异常由 `get()` 传播。
- `Drain` 和 `CancelPending` 两种关闭模式；运行中任务可通过 `std::stop_token` 协作停止。
- 同池 worker 阻塞等待未完成任务时 fail-fast，避免线程饥饿死锁。
- 轻量指标和可选 Observer；ASAN/UBSAN/TSAN、安装消费测试与 benchmark。

## 要求

- CMake 3.25+
- C++20 标准库，并提供 `std::jthread`、`std::stop_source` 和 `std::stop_token`
- GCC/libstdc++ 12+、Clang/libc++ 20+、MSVC 19.36+ 或 Xcode 26+

## 构建与测试

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Sanitizer 使用独立构建：

```bash
cmake --workflow --preset ci-clang-asan-ubsan
cmake --workflow --preset ci-clang-tsan
```

## 使用

```cpp
#include <projecta/concurrency/thread_pool.hpp>

namespace pc = projecta::concurrency;

int main() {
    pc::ThreadPool pool{{.thread_count = 4, .queue_capacity = 1024}};
    auto submitted = pool.submit([](int a, int b) { return a + b; }, 20, 22);
    if (!submitted.has_value()) {
        return 1;
    }
    return std::move(submitted).value().get() == 42 ? 0 : 2;
}
```

限时提交与协作取消：

```cpp
auto task = pool.submit_for(std::chrono::milliseconds(5), [] { /* work */ });
auto stoppable = pool.submit_stoppable([](std::stop_token token) {
    while (!token.stop_requested()) {
        // cooperative work
    }
});
pool.shutdown(pc::ShutdownMode::cancel_pending);
pool.wait();
```

`submit`/`submit_for` 的正常过载通过 `SubmitResult` 表达；内存分配失败仍同步抛异常。参数会 decay-copy 后以右值执行，引用参数必须显式使用 `std::ref`。

## 安装与消费

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release \
  -DPROJECTA_THREAD_POOL_BUILD_TESTS=OFF
cmake --build build/release
cmake --install build/release --prefix /your/prefix
```

下游项目：

```cmake
find_package(ProjectAThreadPool 2 CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE ProjectA::thread_pool)
```

## 语义边界

- FIFO 保证取出顺序，不保证完成顺序或严格公平。
- `CancelPending` 不强杀已经运行的普通任务；stoppable 任务必须自行响应 token。
- 只检测直接的同池阻塞等待，不检测跨线程池依赖环。
- 外部析构同步 Drain；自身 worker 内析构会安全转为异步 Drain。
- 指标字段无数据竞争，但快照不承诺全字段来自同一瞬间。

迁移说明见 [MIGRATION.md](MIGRATION.md)，变更记录见 [CHANGELOG.md](CHANGELOG.md)。

## License

Apache License 2.0，见 [LICENSE](LICENSE)。
