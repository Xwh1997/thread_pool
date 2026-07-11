cmake_minimum_required(VERSION 3.25)
project(ProjectAThreadPoolConsumer LANGUAGES CXX)

find_package(ProjectAThreadPool 2.0 REQUIRED CONFIG)

add_executable(package_consumer main.cpp)
target_link_libraries(package_consumer PRIVATE ProjectA::thread_pool)

enable_testing()
add_test(NAME package_consumer_runs COMMAND package_consumer)
