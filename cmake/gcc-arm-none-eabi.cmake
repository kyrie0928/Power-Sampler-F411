set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)

set(CMAKE_C_COMPILER_ID GNU)
set(CMAKE_CXX_COMPILER_ID GNU)

# Auto-detect arm-none-eabi toolchain in PATH or VS Code EIDE default directory
find_program(ARM_GCC_PATH arm-none-eabi-gcc
    HINTS
        "$ENV{USERPROFILE}/.eide/tools/gcc_arm/bin"
        "$ENV{HOME}/.eide/tools/gcc_arm/bin"
)

if(ARM_GCC_PATH)
    get_filename_component(TOOLCHAIN_BIN_DIR "${ARM_GCC_PATH}" DIRECTORY)
    set(TOOLCHAIN_PREFIX "${TOOLCHAIN_BIN_DIR}/arm-none-eabi-")
else()
    set(TOOLCHAIN_PREFIX "arm-none-eabi-")
endif()

if(WIN32)
    set(TOOL_EXT ".exe")
else()
    set(TOOL_EXT "")
endif()

set(CMAKE_C_COMPILER                "${TOOLCHAIN_PREFIX}gcc${TOOL_EXT}")
set(CMAKE_ASM_COMPILER              "${CMAKE_C_COMPILER}")
set(CMAKE_CXX_COMPILER              "${TOOLCHAIN_PREFIX}g++${TOOL_EXT}")
set(CMAKE_LINKER                    "${TOOLCHAIN_PREFIX}gcc${TOOL_EXT}")
set(CMAKE_OBJCOPY                   "${TOOLCHAIN_PREFIX}objcopy${TOOL_EXT}")
set(CMAKE_SIZE                      "${TOOLCHAIN_PREFIX}size${TOOL_EXT}")

set(CMAKE_EXECUTABLE_SUFFIX_ASM     ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C       ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX     ".elf")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(TARGET_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${TARGET_FLAGS} -fdata-sections -ffunction-sections")
set(CMAKE_ASM_FLAGS "${TARGET_FLAGS} -x assembler-with-cpp -MMD -MP")

set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_C_FLAGS_RELEASE "-O2 -g0")
set(CMAKE_C_FLAGS_MINSIZEREL "-Os -g0")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g3")

set(CMAKE_C_LINK_FLAGS "${TARGET_FLAGS}")
set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -T \"${CMAKE_SOURCE_DIR}/STM32F411CEUx_FLASH.ld\"")
set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} --specs=nano.specs")
set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -Wl,-u,_printf_float")
set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -Wl,-Map=${CMAKE_PROJECT_NAME}.map -Wl,--gc-sections")
set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -Wl,--start-group -lc -lm -Wl,--end-group")
set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -Wl,--print-memory-usage")
