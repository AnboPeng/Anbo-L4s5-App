# ==================================================================
# ARM Embedded Toolchain — arm-none-eabi-gcc
# ==================================================================
# Auto-downloads the ARM GNU Toolchain if it is not found on PATH
# or in a user-specified location.
#
# Usage:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
#
# Options:
#   -DARM_TOOLCHAIN_PATH=<dir>      Point to an existing bin/ directory
#   -DARM_TC_VERSION=13.3.rel1      Override the toolchain release tag
# ==================================================================
cmake_minimum_required(VERSION 3.18)

set(CMAKE_SYSTEM_NAME       Generic)
set(CMAKE_SYSTEM_PROCESSOR  arm)

# ---- Toolchain version (user-overridable) ----
if(NOT DEFINED ARM_TC_VERSION)
    set(ARM_TC_VERSION "13.3.rel1")
endif()

# ---- Platform-specific archive names ----
set(_TOOLS_DIR "${CMAKE_CURRENT_LIST_DIR}/../tools")

if(CMAKE_HOST_WIN32)
    set(_TC_ARCHIVE   "arm-gnu-toolchain-${ARM_TC_VERSION}-mingw-w64-i686-arm-none-eabi.zip")
    set(_TC_EXTRACTED "arm-gnu-toolchain-${ARM_TC_VERSION}-mingw-w64-i686-arm-none-eabi")
    set(_EXE ".exe")
elseif(CMAKE_HOST_APPLE)
    # Detect Apple Silicon vs Intel
    execute_process(COMMAND uname -m OUTPUT_VARIABLE _HOST_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_HOST_ARCH STREQUAL "arm64")
        set(_TC_ARCHIVE   "arm-gnu-toolchain-${ARM_TC_VERSION}-darwin-arm64-arm-none-eabi.tar.xz")
        set(_TC_EXTRACTED "arm-gnu-toolchain-${ARM_TC_VERSION}-darwin-arm64-arm-none-eabi")
    else()
        set(_TC_ARCHIVE   "arm-gnu-toolchain-${ARM_TC_VERSION}-darwin-x86_64-arm-none-eabi.tar.xz")
        set(_TC_EXTRACTED "arm-gnu-toolchain-${ARM_TC_VERSION}-darwin-x86_64-arm-none-eabi")
    endif()
    set(_EXE "")
else()
    # Linux
    execute_process(COMMAND uname -m OUTPUT_VARIABLE _HOST_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_HOST_ARCH STREQUAL "aarch64")
        set(_TC_ARCHIVE   "arm-gnu-toolchain-${ARM_TC_VERSION}-aarch64-arm-none-eabi.tar.xz")
        set(_TC_EXTRACTED "arm-gnu-toolchain-${ARM_TC_VERSION}-aarch64-arm-none-eabi")
    else()
        set(_TC_ARCHIVE   "arm-gnu-toolchain-${ARM_TC_VERSION}-x86_64-arm-none-eabi.tar.xz")
        set(_TC_EXTRACTED "arm-gnu-toolchain-${ARM_TC_VERSION}-x86_64-arm-none-eabi")
    endif()
    set(_EXE "")
endif()

set(_TC_DIR "${_TOOLS_DIR}/${_TC_EXTRACTED}")
set(_TC_GCC "${_TC_DIR}/bin/arm-none-eabi-gcc${_EXE}")

# ---- Resolve toolchain location (priority order) ----
#  1. User override:  -DARM_TOOLCHAIN_PATH=<path-to-bin>
#  2. Local tools/ :  previously auto-downloaded
#  3. System PATH  :  globally installed
#  4. Auto-download:  fetch from developer.arm.com

if(DEFINED ARM_TOOLCHAIN_PATH)
    set(_PREFIX "${ARM_TOOLCHAIN_PATH}/")
    message(STATUS "[Toolchain] User-specified: ${ARM_TOOLCHAIN_PATH}")

elseif(EXISTS "${_TC_GCC}")
    set(_PREFIX "${_TC_DIR}/bin/")
    message(STATUS "[Toolchain] Local: ${_TC_DIR}")

else()
    find_program(_SYS_GCC arm-none-eabi-gcc)
    if(_SYS_GCC)
        set(_PREFIX "")
        message(STATUS "[Toolchain] System PATH: ${_SYS_GCC}")
    else()
        # ---- Auto-download ----
        set(_TC_MARKER "${_TC_DIR}/.tc_ready")
        if(NOT EXISTS "${_TC_MARKER}")
            set(_TC_URL
                "https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_TC_VERSION}/binrel/${_TC_ARCHIVE}")
            set(_TC_DL "${_TOOLS_DIR}/${_TC_ARCHIVE}")

            message(STATUS "")
            message(STATUS "============================================================")
            message(STATUS "[Toolchain] arm-none-eabi-gcc not found")
            message(STATUS "[Toolchain] Downloading ARM GNU Toolchain ${ARM_TC_VERSION} ...")
            message(STATUS "[Toolchain] URL: ${_TC_URL}")
            message(STATUS "[Toolchain] This may take a few minutes (~300 MB) ...")
            message(STATUS "============================================================")
            message(STATUS "")

            file(MAKE_DIRECTORY "${_TOOLS_DIR}")
            file(DOWNLOAD "${_TC_URL}" "${_TC_DL}"
                 STATUS _dl_status
                 SHOW_PROGRESS
                 TLS_VERIFY ON)
            list(GET _dl_status 0 _dl_code)
            if(NOT _dl_code EQUAL 0)
                list(GET _dl_status 1 _dl_msg)
                message(FATAL_ERROR
                    "[Toolchain] Download failed (${_dl_code}): ${_dl_msg}\n"
                    "            URL: ${_TC_URL}\n"
                    "  Install manually and re-run, or use -DARM_TOOLCHAIN_PATH=<path>")
            endif()

            message(STATUS "[Toolchain] Extracting ${_TC_ARCHIVE} ...")
            file(ARCHIVE_EXTRACT INPUT "${_TC_DL}" DESTINATION "${_TOOLS_DIR}")
            file(REMOVE "${_TC_DL}")

            if(NOT EXISTS "${_TC_GCC}")
                message(FATAL_ERROR
                    "[Toolchain] Extraction succeeded but ${_TC_GCC} not found.\n"
                    "            Check the extracted directory under ${_TOOLS_DIR}")
            endif()

            file(WRITE "${_TC_MARKER}" "version=${ARM_TC_VERSION}\n")
            message(STATUS "[Toolchain] ARM GNU Toolchain ${ARM_TC_VERSION} ready at ${_TC_DIR}")
        endif()

        set(_PREFIX "${_TC_DIR}/bin/")
    endif()
endif()

# ---- Toolchain executables ----
# When _PREFIX is a full path, append .exe on Windows so CMake resolves it.
set(CMAKE_C_COMPILER   "${_PREFIX}arm-none-eabi-gcc${_EXE}"     CACHE STRING "" FORCE)
set(CMAKE_CXX_COMPILER "${_PREFIX}arm-none-eabi-g++${_EXE}"     CACHE STRING "" FORCE)
set(CMAKE_ASM_COMPILER "${_PREFIX}arm-none-eabi-gcc${_EXE}"     CACHE STRING "" FORCE)
set(CMAKE_OBJCOPY      "${_PREFIX}arm-none-eabi-objcopy${_EXE}" CACHE STRING "" FORCE)
set(CMAKE_OBJDUMP      "${_PREFIX}arm-none-eabi-objdump${_EXE}" CACHE STRING "" FORCE)
set(CMAKE_SIZE         "${_PREFIX}arm-none-eabi-size${_EXE}"    CACHE STRING "" FORCE)
set(CMAKE_AR           "${_PREFIX}arm-none-eabi-ar${_EXE}"      CACHE STRING "" FORCE)

# ---- CPU / FPU flags (Cortex-M4F with single-precision FPU) ----
set(CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")

# ---- Common compiler flags ----
set(CMAKE_C_FLAGS_INIT
    "${CPU_FLAGS} -fdata-sections -ffunction-sections -fno-common -fno-exceptions"
    CACHE STRING "" FORCE)

set(CMAKE_CXX_FLAGS_INIT
    "${CPU_FLAGS} -fdata-sections -ffunction-sections -fno-common -fno-exceptions -fno-rtti"
    CACHE STRING "" FORCE)

set(CMAKE_ASM_FLAGS_INIT
    "${CPU_FLAGS} -x assembler-with-cpp"
    CACHE STRING "" FORCE)

# ---- Linker flags ----
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "${CPU_FLAGS} -Wl,--gc-sections -Wl,--no-warn-rwx-segments -specs=nano.specs -specs=nosys.specs -lnosys"
    CACHE STRING "" FORCE)

# ---- Build types ----
set(CMAKE_C_FLAGS_DEBUG          "-Og -g3 -DDEBUG"  CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_RELEASE        "-O2 -DNDEBUG"     CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_MINSIZEREL     "-Os -DNDEBUG"     CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG"  CACHE STRING "" FORCE)

# ---- Prevent CMake from testing the compiler with a full link ----
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ---- Disable host-system search paths ----
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
