# Changelog

## 2.0.0

- 升级到 C++20，并发布 `ProjectA::thread_pool` CMake 包。
- 新增有界 FIFO、立即/限时背压、move-only 任务和 `TaskHandle`。
- 新增 Drain、CancelPending、stop token 和同池等待检测。
- 新增 Metrics、Observer、sanitizer、benchmark 和三平台 CI。
- 删除 v1 全局 `ThreadPool` 与公开 `ThreadSafeQueue` API。
