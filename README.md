# ProjectA

一个用 **C++17** 写的练手小工程：实现**线程池**和**线程安全队列**，编译成静态库 **`my_thread`**，并用 **GoogleTest** 做单元测试。

你可以把它当成「多线程基础组件」的起点，在自己的程序里链接 `my_thread` 使用线程池。

---

## 目录里有什么

| 路径 | 说明 |
|------|------|
| `include/ThreadPool.h` | 线程池类声明与 `submit` 模板实现 |
| `include/ThreadSafeQueue.h` | 模板化的线程安全阻塞队列 |
| `source/ThreadPool.cpp` | 线程池构造、析构（worker 线程逻辑） |
| `test/thread_pool_test.cpp` | GTest 用例：正常提交、异常、`submit` 嵌套、并发压测等 |
| `CMakeLists.txt` | 根构建脚本（C++17、可选 ASAN/TSAN、测试与 GTest） |
| `vcpkg.json` | vcpkg **清单模式**依赖：声明需要 `gtest` |

---

## 你需要什么环境

- **CMake 3.18 或以上**（工程里用了 GTest 的 `DISCOVERY_MODE PRE_TEST`，需要较新的 CMake 模块。）
- 支持 **C++17** 的编译器：**GCC**、**Clang** 或 **MSVC**。
- **单元测试依赖 GTest**，有两种来源（二选一即可）：
  - 用 **vcpkg** 安装（见下文「方式 A」）；
  - 不配 vcpkg 时，CMake 会通过 **FetchContent** 自动从 GitHub 拉 **googletest v1.14.0**（需要能访问外网）。

说明：**Thread Sanitizer（TSAN）** 仅在 **GCC / Clang / AppleClang** 下开启；MSVC 下若打开 `ENABLE_TSAN` 会配置失败。

---

## 怎么编译

下面命令都在**项目根目录**执行。第一次会生成 `build` 目录。

### 方式 A：用 vcpkg（推荐，离线/内网镜像也更可控）

把 `<VCPKG_ROOT>` 换成你本机 vcpkg 的路径（例如 `~/vcpkg`）：

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=<VCPKG_ROOT>/scripts/buildsystems/vcpkg.cmake
cmake --build build -j$(nproc)
```

若已设置环境变量 **`VCPKG_ROOT`**，且配置时没有手动指定工具链，根目录 `CMakeLists.txt` 会尝试自动使用 `$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`。

### 方式 B：不用 vcpkg

不传 `CMAKE_TOOLCHAIN_FILE`（或在新目录里第一次配置，避免沿用上一次的 vcpkg 缓存）：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

首次配置若找不到已安装的 GTest，会自动 **FetchContent** 下载 GoogleTest（需网络）。

### 只做库、不要测试

```bash
cmake -S . -B build -DBUILD_TESTING=OFF
cmake --build build
```

---

## 怎么跑测试

编译成功后：

```bash
ctest --test-dir build --output-on-failure
```

也可以直接运行测试程序（路径以你生成目录为准）：

```bash
./build/test/unit_tests
```

测试代码集中在 `test/thread_pool_test.cpp`，覆盖例如：有返回值/void 任务、任务内异常通过 `future` 传出、多线程同时 `submit`、worker 里再 `submit` 并等待（容易踩死锁的场景）、大量短任务压测等。

---

## CMake 选项速查

| 选项 | 默认 | 含义 |
|------|------|------|
| `ENABLE_ASAN` | `ON` | 打开 **AddressSanitizer**（内存错误更易暴露）。 |
| `ENABLE_TSAN` | `OFF` | 打开 **ThreadSanitizer**（数据竞争等；与 ASAN **不能同时开**）。 |
| `BUILD_TESTING` | `ON` | 是否编译 `unit_tests`。 |

**注意：** `ENABLE_ASAN` 与 `ENABLE_TSAN` 互斥。开 TSAN 时要关掉 ASAN，例如：

```bash
cmake -S . -B build -DENABLE_ASAN=OFF -DENABLE_TSAN=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

做 **Release / 性能** 向构建时，一般关掉 ASAN：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_ASAN=OFF
```

### WSL2 与 TSAN、GTest 用例发现

在 **WSL2** 上，若构建阶段就去执行带 TSAN 的测试程序做「用例枚举」，有时会出现 **`FATAL: ThreadSanitizer: unexpected memory mapping`**。  
本仓库在 **`ENABLE_TSAN=ON`** 时，对 `gtest_discover_tests` 使用了 **`DISCOVERY_MODE PRE_TEST`**（见 `test/CMakeLists.txt`）：**编译链接阶段不再跑测试二进制**，改为在 **`ctest` 真正跑测时** 再发现用例，从而避开这类问题。

---

## 最小使用示例（伪代码）

```cpp
#include <ThreadPool.h>

void example() {
    ThreadPool pool(4);
    auto future = pool.submit([](int a, int b) { return a + b; }, 1, 2);
    int result = future.get();  // 3
}
```

在自己的 CMake 工程里：`target_link_libraries(你的目标 PRIVATE my_thread)`，并保证能包含 `include/`（本仓库里 `my_thread` 已把该目录设为 `PUBLIC` 头文件路径）。

---

## 常见问题

1. **配置时找不到 GTest**  
   确认是否带了 vcpkg 的 `CMAKE_TOOLCHAIN_FILE`；或检查 FetchContent 是否能访问 GitHub。

2. **开 TSAN 后构建报错、或提示 ThreadSanitizer memory mapping**  
   使用本仓库默认的 **PRE_TEST** 配置后，应能正常 **build**；请用 **`ctest`** 跑测试。若仍异常，可查阅 Clang/GCC 文档与 WSL 相关讨论。

3. **想换编译器**  
   例如：`-DCMAKE_CXX_COMPILER=g++-12`，重新在一个空 `build` 目录配置。

---

## 许可证

仓库内**未附带**许可证文件；若对外发布或协作，建议自行添加 `LICENSE` 并在此 README 中写明。
