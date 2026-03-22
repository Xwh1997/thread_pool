cmake_minimum_required(VERSION 3.18)

# vcpkg：
# - 工具链：-DCMAKE_TOOLCHAIN_FILE=<VCPKG_ROOT>/scripts/buildsystems/vcpkg.cmake
# - 本仓库根目录的 vcpkg.json 为 manifest 模式依赖声明（gtest）
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
      CACHE STRING "vcpkg toolchain")
endif()

project(ProjectA VERSION 1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_ASAN "启用 AddressSanitizer（与 TSAN 互斥）" ON)
option(ENABLE_TSAN "启用 ThreadSanitizer：数据竞争与死锁类问题由运行时报告；需配合 GTest 跑通（与 ASAN 互斥）" OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
  message(FATAL_ERROR "ENABLE_ASAN 与 ENABLE_TSAN 不能同时开启，请使用 -DENABLE_ASAN=OFF -DENABLE_TSAN=ON")
endif()

if(ENABLE_ASAN)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
  elseif(MSVC)
    add_compile_options(/fsanitize:address)
    add_link_options(/INCREMENTAL:NO /fsanitize:address)
  endif()
endif()

if(ENABLE_TSAN)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-fsanitize=thread -g -O1)
    add_link_options(-fsanitize=thread)
  else()
    message(FATAL_ERROR "ENABLE_TSAN 目前仅支持 GCC / Clang / AppleClang（MSVC 请用其他方式做线程检测）")
  endif()
endif()

add_subdirectory(source)

option(BUILD_TESTING "编译单元测试" ON)
include(CTest)
if(BUILD_TESTING)
  find_package(GTest CONFIG QUIET)
  if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()
  add_subdirectory(test)
endif()
