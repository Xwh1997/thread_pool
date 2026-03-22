cmake_minimum_required(VERSION 3.15)

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

option(ENABLE_ASAN "启用 AddressSanitizer" ON)
if(ENABLE_ASAN)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
  elseif(MSVC)
    add_compile_options(/fsanitize=address)
    add_link_options(/INCREMENTAL:NO /fsanitize=address)
  endif()
endif()

add_subdirectory(source)

option(BUILD_TESTING "编译单元测试" ON)
include(CTest)
if(BUILD_TESTING)
  # 优先使用 vcpkg / 系统已安装的 GTest；未找到时再拉取源码（无需 CMAKE_TOOLCHAIN_FILE）
  find_package(GTest CONFIG QUIET)
  add_subdirectory(test)
endif()
