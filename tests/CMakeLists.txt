add_executable(canbus_tests
  test_main.cpp
  test_candump.cpp
  # TESTS
)
target_link_libraries(canbus_tests PRIVATE canbus_core)
target_compile_definitions(canbus_tests PRIVATE CANBUS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

foreach(suite candump # SUITES
        )
  add_test(NAME ${suite} COMMAND canbus_tests ${suite})
endforeach()
