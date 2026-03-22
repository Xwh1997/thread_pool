add_test( Smoke.Basic /home/xwh/Project/ProjectA/build/test/unit_tests [==[--gtest_filter=Smoke.Basic]==] --gtest_also_run_disabled_tests)
set_tests_properties( Smoke.Basic PROPERTIES WORKING_DIRECTORY /home/xwh/Project/ProjectA/build/test SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==])
set( unit_tests_TESTS Smoke.Basic)
