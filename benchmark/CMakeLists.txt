find_package(benchmark CONFIG QUIET)
if(NOT benchmark_FOUND)
  if(NOT PROJECTA_THREAD_POOL_FETCH_TEST_DEPS)
    message(FATAL_ERROR "Google Benchmark was not found and dependency fetching is disabled")
  endif()
  include(FetchContent)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.1
  )
  FetchContent_MakeAvailable(googlebenchmark)
endif()

add_executable(thread_pool_benchmarks thread_pool_benchmark.cpp)
target_link_libraries(thread_pool_benchmarks PRIVATE
  ProjectA::thread_pool benchmark::benchmark_main)
projecta_thread_pool_apply_build_options(thread_pool_benchmarks)

if(PROJECTA_THREAD_POOL_BUILD_TESTS)
  add_test(NAME benchmark_smoke
    COMMAND thread_pool_benchmarks --benchmark_min_time=0.01 --benchmark_repetitions=1)
  set_tests_properties(benchmark_smoke PROPERTIES LABELS benchmark TIMEOUT 30)
endif()
