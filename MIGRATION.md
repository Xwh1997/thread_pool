# Migrating from v1 to v2

v2 是破坏性升级，不提供旧 API 兼容层。

| v1 | v2 |
|---|---|
| `#include <ThreadPool.h>` | `#include <projecta/concurrency/thread_pool.hpp>` |
| 全局 `ThreadPool` | `projecta::concurrency::ThreadPool` |
| `ThreadPool(4)` | `ThreadPool{{.thread_count = 4, .queue_capacity = N}}` |
| `std::future<R> submit(...)` | `SubmitResult<R> submit(...)`，成功后取得 `TaskHandle<R>` |
| 无界队列 | 必须选择有限的 `queue_capacity` |
| 析构隐式排空 | 仍默认排空，并新增显式 `shutdown`/`wait` |

原 `ThreadSafeQueue` 是实现细节，v2 不再安装或承诺其 API。worker 不得阻塞等待同一 pool 中尚未完成的 handle；此操作现在会报告 `WouldDeadlock`。
