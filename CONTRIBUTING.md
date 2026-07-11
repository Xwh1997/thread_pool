# Contributing

1. 使用 CMake 3.25+ 和支持所需 C++20 并发库特性的工具链。
2. 行为修改必须先添加会失败的确定性测试；并发测试使用 latch/barrier/promise，不用任意 sleep 证明正确性。
3. 提交前运行 `cmake --workflow --preset ci-gcc`，并按修改风险运行 ASAN/UBSAN 或 TSAN preset。
4. 不在持有线程池内部锁时调用用户代码或 Observer。
5. 公共 API 变更必须更新 README、MIGRATION 和 CHANGELOG。
