add_executable(unit_tests
  thread_pool_contract_test.cpp
  thread_pool_backpressure_test.cpp
  thread_pool_lifecycle_test.cpp
  thread_pool_observability_test.cpp
  thread_pool_stress_test.cpp
)
target_link_libraries(unit_tests PRIVATE ProjectA::thread_pool GTest::gtest_main)
projecta_thread_pool_apply_build_options(unit_tests)
projecta_thread_pool_apply_sanitizers(unit_tests)

include(GoogleTest)
if(PROJECTA_THREAD_POOL_ENABLE_TSAN)
  gtest_discover_tests(unit_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES
      TIMEOUT 10
      ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1"
  )
else()
  gtest_discover_tests(unit_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 10)
endif()
