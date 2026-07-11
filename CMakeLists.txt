cmake_minimum_required(VERSION 3.25)

project(ProjectAThreadPool VERSION 2.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(Threads REQUIRED)

option(PROJECTA_THREAD_POOL_BUILD_TESTS "Build ProjectA ThreadPool tests" ${PROJECT_IS_TOP_LEVEL})
option(PROJECTA_THREAD_POOL_BUILD_BENCHMARKS "Build ProjectA ThreadPool benchmarks" OFF)
option(PROJECTA_THREAD_POOL_FETCH_TEST_DEPS "Fetch missing test dependencies" ${PROJECT_IS_TOP_LEVEL})
option(PROJECTA_THREAD_POOL_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(PROJECTA_THREAD_POOL_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(PROJECTA_THREAD_POOL_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(PROJECTA_THREAD_POOL_ENABLE_COVERAGE "Enable coverage instrumentation" OFF)

if(PROJECTA_THREAD_POOL_ENABLE_TSAN AND
   (PROJECTA_THREAD_POOL_ENABLE_ASAN OR PROJECTA_THREAD_POOL_ENABLE_UBSAN))
  message(FATAL_ERROR "TSAN cannot be combined with ASAN or UBSAN")
endif()

function(projecta_thread_pool_apply_build_options target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  endif()
endfunction()

function(projecta_thread_pool_apply_sanitizers target)
  if(PROJECTA_THREAD_POOL_ENABLE_ASAN)
    if(MSVC)
      target_compile_options(${target} PRIVATE /fsanitize=address)
      target_link_options(${target} PRIVATE /INCREMENTAL:NO)
    else()
      target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=address)
    endif()
  endif()
  if(PROJECTA_THREAD_POOL_ENABLE_UBSAN)
    if(MSVC)
      message(FATAL_ERROR "UBSAN is not supported by this project on MSVC")
    endif()
    target_compile_options(${target} PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=undefined)
  endif()
  if(PROJECTA_THREAD_POOL_ENABLE_TSAN)
    if(MSVC)
      message(FATAL_ERROR "TSAN is not supported by this project on MSVC")
    endif()
    target_compile_options(${target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=thread)
  endif()
  if(PROJECTA_THREAD_POOL_ENABLE_COVERAGE)
    if(MSVC)
      message(FATAL_ERROR "Coverage instrumentation is not supported by this project on MSVC")
    endif()
    target_compile_options(${target} PRIVATE --coverage -O0 -g)
    target_link_options(${target} PRIVATE --coverage)
  endif()
endfunction()

add_subdirectory(source)

if(PROJECTA_THREAD_POOL_BUILD_TESTS)
  include(CTest)
  find_package(GTest CONFIG QUIET)
  if(NOT GTest_FOUND)
    if(NOT PROJECTA_THREAD_POOL_FETCH_TEST_DEPS)
      message(FATAL_ERROR "GTest was not found and dependency fetching is disabled")
    endif()
    include(FetchContent)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
  endif()
  add_subdirectory(test)
endif()

if(PROJECTA_THREAD_POOL_BUILD_BENCHMARKS)
  add_subdirectory(benchmark)
endif()

configure_package_config_file(
  cmake/ProjectAThreadPoolConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/ProjectAThreadPoolConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ProjectAThreadPool
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/ProjectAThreadPoolConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(EXPORT ProjectAThreadPoolTargets
  FILE ProjectAThreadPoolTargets.cmake
  NAMESPACE ProjectA::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ProjectAThreadPool
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/ProjectAThreadPoolConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/ProjectAThreadPoolConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ProjectAThreadPool
)
