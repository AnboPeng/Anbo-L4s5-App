# ==================================================================
# Top-level CMake — l4s5_Anbo project
# ==================================================================
# Build (toolchain auto-downloads if not found):
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
#   cmake --build build
#
# Options:
#   -DARM_TOOLCHAIN_PATH=<dir>  Use a specific arm-none-eabi-gcc bin/
#   -DARM_TC_VERSION=13.3.rel1  Override the ARM toolchain release
# ==================================================================
cmake_minimum_required(VERSION 3.18)
project(l4s5_Anbo
    VERSION 0.1.0        # Default version (fallback)
    LANGUAGES C ASM
)

# ---- Firmware Version ----
# Override at configure time:  cmake -DFW_VERSION=1.2.3 ...
# If not provided, falls back to project(VERSION ...) above.
# CI reads project(VERSION ...) as the source of truth for auto-tag/release.
set(FW_VERSION "${PROJECT_VERSION}" CACHE STRING "Firmware version (MAJOR.MINOR.PATCH)")

if(NOT FW_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    message(FATAL_ERROR "FW_VERSION '${FW_VERSION}' is not valid semver (x.y.z)")
endif()
set(FW_VERSION_MAJOR ${CMAKE_MATCH_1})
set(FW_VERSION_MINOR ${CMAKE_MATCH_2})
set(FW_VERSION_PATCH ${CMAKE_MATCH_3})

add_compile_definitions(
    FW_VERSION_MAJOR=${FW_VERSION_MAJOR}
    FW_VERSION_MINOR=${FW_VERSION_MINOR}
    FW_VERSION_PATCH=${FW_VERSION_PATCH}
    FW_VERSION_STRING="${FW_VERSION}"
)

# ---- Kernel + Port libraries ----
add_subdirectory(anbo)

# ---- Firmware executable ----
set(FIRMWARE l4s5_anbo)

# Output with .elf extension (arm-none-eabi convention)
set(CMAKE_EXECUTABLE_SUFFIX .elf)

add_executable(${FIRMWARE}
    app/main.c
    app/app_config.c
    app/app_fault_mgr.c
    app/app_sensor.c
    app/app_controller.c
    app/app_ui.c
    app/app_sleep.c
    anbo/port/stm32l4s5/syscalls.c
)

target_link_libraries(${FIRMWARE} PRIVATE
    anbo_port_stm32l4s5
    anbo_kernel
)

# Linker script
target_link_options(${FIRMWARE} PRIVATE
    -T${PORT_LINKER_SCRIPT}
    -Wl,-Map=${CMAKE_BINARY_DIR}/${FIRMWARE}.map,--cref
    -Wl,--print-memory-usage
)

# ---- Post-build: generate .bin and .hex ----
add_custom_command(TARGET ${FIRMWARE} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${FIRMWARE}>
            ${CMAKE_BINARY_DIR}/${FIRMWARE}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${FIRMWARE}>
            ${CMAKE_BINARY_DIR}/${FIRMWARE}.hex
    COMMAND ${CMAKE_SIZE}    $<TARGET_FILE:${FIRMWARE}>
    COMMENT "Generating ${FIRMWARE}.bin / .hex and printing size"
)
