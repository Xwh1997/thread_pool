add_executable(unit_tests thread_pool_test.cpp)
target_link_libraries(unit_tests PRIVATE my_thread GTest::gtest_main)

include(GoogleTest)
if(ENABLE_TSAN)
  gtest_discover_tests(unit_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES
      ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1"
  )
else()
  gtest_discover_tests(unit_tests)
endif()
