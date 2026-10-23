# Purpose: Build the production dashboard CAN, logging and LVGL bindings against host-only
# fake peripherals and a memory framebuffer, without compiling firmware startup or RTOS tasks.
cmake_minimum_required(VERSION 3.20)
project(dashboard_host LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
get_filename_component(DASHBOARD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# Compile the checked-in LVGL core/widgets/draw engines; exclude demos/examples/tests.
# The generated ui target consumes the same library and lv_conf.h as the host renderer.
file(GLOB_RECURSE DASHBOARD_LVGL_SOURCES CONFIGURE_DEPENDS "${DASHBOARD_ROOT}/lvgl/src/*.c")
list(FILTER DASHBOARD_LVGL_SOURCES EXCLUDE REGEX "/(demos|examples|tests)/")
add_library(lvgl STATIC ${DASHBOARD_LVGL_SOURCES})
target_include_directories(lvgl SYSTEM PUBLIC "${DASHBOARD_ROOT}" "${DASHBOARD_ROOT}/lvgl")
target_compile_definitions(lvgl PUBLIC LV_LVGL_H_INCLUDE_SIMPLE LV_CONF_INCLUDE_SIMPLE)
add_subdirectory("${DASHBOARD_ROOT}/ui" "${CMAKE_CURRENT_BINARY_DIR}/ui")
target_include_directories(ui PRIVATE "${DASHBOARD_ROOT}")
target_link_libraries(ui PUBLIC lvgl)

# Production algorithms are compiled directly. Host include ordering supplies fake HAL,
# BSP and RTOS interfaces in place of hardware headers; startup/tasks/interrupts are excluded.
add_executable(dashboard_host
    main.c fake_hal.c render.c
    "${DASHBOARD_ROOT}/Src/app_can.c"
    "${DASHBOARD_ROOT}/Src/app_can_config.c"
    "${DASHBOARD_ROOT}/Src/app_data_log.c"
    "${DASHBOARD_ROOT}/Src/app_emmc_log.c"
    "${DASHBOARD_ROOT}/Src/app_dashboard_ui.c")
target_include_directories(dashboard_host PRIVATE
    include "${DASHBOARD_ROOT}/tests/can_host/include"
    "${DASHBOARD_ROOT}/Inc" "${DASHBOARD_ROOT}" "${DASHBOARD_ROOT}/ui")
target_compile_options(dashboard_host PRIVATE -Wall -Wextra -Werror -Wpedantic)
target_link_libraries(dashboard_host PRIVATE ui lvgl m)

# Instrument host, generated UI and upstream LVGL together so bad RX copies or pixel
# accesses cross no unchecked library boundary. Unsupported compiler IDs keep normal flags.
option(DASHBOARD_HOST_SANITIZERS "Enable address and undefined behavior sanitizers" ON)
if(DASHBOARD_HOST_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    foreach(target dashboard_host lvgl ui)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    endforeach()
    target_link_options(dashboard_host PRIVATE -fsanitize=address,undefined)
endif()

# Exercise the actual UI binding with test-controlled log status and renderer output.
enable_testing()
add_executable(dashboard_ui_tests
    "${DASHBOARD_ROOT}/tests/dashboard_ui/test_binding.c"
    render.c "${DASHBOARD_ROOT}/Src/app_dashboard_ui.c")
target_include_directories(dashboard_ui_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${DASHBOARD_ROOT}/Inc" "${DASHBOARD_ROOT}")
target_compile_options(dashboard_ui_tests PRIVATE -Wall -Wextra -Werror -Wpedantic -UNDEBUG)
target_link_libraries(dashboard_ui_tests PRIVATE ui lvgl m)
if(DASHBOARD_HOST_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(dashboard_ui_tests PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(dashboard_ui_tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME dashboard_ui_bindings COMMAND dashboard_ui_tests
    "${CMAKE_CURRENT_BINARY_DIR}/ui-binding-test.ppm")
