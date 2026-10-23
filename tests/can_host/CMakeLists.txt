# Purpose: compile the production CAN driver in a deterministic host test harness with sanitizers.
cmake_minimum_required(VERSION 3.20)
project(can_host_tests LANGUAGES C)
enable_testing()
# The harness includes production CAN/config C itself to test private helpers and retain
# one implementation of decoding. Local fake HAL headers provide only the accessed ABI.
add_executable(can_host_tests test_app_can.c)
target_compile_features(can_host_tests PRIVATE c_std_11)
target_include_directories(can_host_tests PRIVATE include ../../Inc)
target_compile_options(can_host_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
option(CAN_HOST_SANITIZERS "Enable address and undefined behavior sanitizers" ON)
if(CAN_HOST_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(can_host_tests PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  target_link_options(can_host_tests PRIVATE -fsanitize=address,undefined)
endif()
# Register the complete assertion suite and a separate maximum-DLC bounds regression.
add_test(NAME can_driver_suite COMMAND can_host_tests)
add_test(NAME can_driver_payload_bounds COMMAND can_host_tests --overflow)
