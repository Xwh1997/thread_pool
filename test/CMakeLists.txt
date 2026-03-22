add_executable(unit_tests thread_pool_test.cpp)
target_link_libraries(unit_tests PRIVATE my_thread GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(unit_tests)
