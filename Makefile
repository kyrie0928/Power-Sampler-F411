# GNU Makefile for Power-Sampler-F411 (STM32F411CEU6)
# Works out-of-the-box with ARM GNU Toolchain / VS Code EIDE tools.

TARGET      := power_sampler_f411
BUILD_DIR   := build/make
OUT_DIR     := $(BUILD_DIR)/output

TOOLCHAIN_DIR ?=
TOOLCHAIN_DIR := $(subst \,/,$(TOOLCHAIN_DIR))
ifneq ($(strip $(TOOLCHAIN_DIR)),)
PREFIX := $(TOOLCHAIN_DIR)/arm-none-eabi-
else
PREFIX := arm-none-eabi-
endif

CC      := $(PREFIX)gcc
AS      := $(PREFIX)gcc -x assembler-with-cpp
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size

CPU_FLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
OPT_FLAGS ?= -O2 -g3

DEFS := -DUSE_HAL_DRIVER -DSTM32F411xE

INCLUDES := \
	-ICore/Inc \
	-IBSP/Inc \
	-IDriver/Inc \
	-IService/Inc \
	-IApp/Inc \
	-IDrivers/STM32F4xx_HAL_Driver/Inc \
	-IDrivers/STM32F4xx_HAL_Driver/Inc/Legacy \
	-IDrivers/CMSIS/Device/ST/STM32F4xx/Include \
	-IDrivers/CMSIS/Include

COMMON_CFLAGS := $(CPU_FLAGS) $(OPT_FLAGS) $(DEFS) $(INCLUDES) -std=gnu11 -fdata-sections -ffunction-sections -MMD -MP
HAL_CFLAGS    := $(COMMON_CFLAGS) -Wall -Wno-unused-parameter
APP_CFLAGS    := $(COMMON_CFLAGS) -Wall -Wextra -Werror
ASFLAGS       := $(CPU_FLAGS) -MMD -MP

LDFLAGS := $(CPU_FLAGS) \
	-TSTM32F411CEUx_FLASH.ld \
	--specs=nano.specs \
	-Wl,-u,_printf_float \
	-Wl,-Map=$(OUT_DIR)/$(TARGET).map,--gc-sections,--print-memory-usage \
	-Wl,--start-group -lc -lm -Wl,--end-group

HAL_C_SRCS := \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_adc.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_adc_ex.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma_ex.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_exti.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ex.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ramfunc.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c_ex.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_spi.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim_ex.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_adc.c \
	Core/Src/system_stm32f4xx.c \
	Core/Src/syscalls.c \
	Core/Src/sysmem.c

APP_C_SRCS := \
	Core/Src/main.c \
	Core/Src/stm32f4xx_hal_msp.c \
	Core/Src/stm32f4xx_it.c \
	BSP/Src/bsp_gpio.c \
	BSP/Src/bsp_timebase.c \
	BSP/Src/bsp_adc_dma.c \
	BSP/Src/bsp_spi_bus.c \
	BSP/Src/bsp_i2c_bus.c \
	BSP/Src/bsp_uart.c \
	Driver/Src/drv_shunt_range.c \
	Driver/Src/drv_ads1220.c \
	Driver/Src/drv_eeprom_24c02.c \
	Service/Src/svc_calib_store.c \
	Service/Src/svc_sampler.c \
	Service/Src/svc_host_proto.c \
	App/Src/app_diag_cli.c \
	App/Src/app_main.c

ASM_SRCS := startup_stm32f411xe.s

HAL_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(HAL_C_SRCS))
APP_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(APP_C_SRCS))
ASM_OBJS := $(patsubst %.s,$(BUILD_DIR)/%.o,$(ASM_SRCS))
ALL_OBJS := $(HAL_OBJS) $(APP_OBJS) $(ASM_OBJS)
DEPS     := $(ALL_OBJS:.o=.d)

.PHONY: all clean

all: $(OUT_DIR)/$(TARGET).elf $(OUT_DIR)/$(TARGET).hex $(OUT_DIR)/$(TARGET).bin

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(if $(filter $(APP_C_SRCS),$<),$(APP_CFLAGS),$(HAL_CFLAGS)) -c $< -o $@

$(BUILD_DIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

$(OUT_DIR)/$(TARGET).elf: $(ALL_OBJS) STM32F411CEUx_FLASH.ld
	@mkdir -p $(OUT_DIR)
	$(CC) $(ALL_OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(OUT_DIR)/$(TARGET).hex: $(OUT_DIR)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(OUT_DIR)/$(TARGET).bin: $(OUT_DIR)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
