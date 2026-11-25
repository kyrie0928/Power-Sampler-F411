# Power-Sampler-F411 工程代理指南 (`AGENTS.md`)

> 每次在本仓库处理任务前，先阅读本文件与 [PROJECT_CONTEXT.md](.agents/PROJECT_CONTEXT.md)；完整硬件引脚定义、三挡校准 SOP 与 ESP32 串口帧格式见 [README.md](README.md)。

---

## 1. 项目定位

- **目标硬件**：基于嘉立创EDA改版工程 **`采样pcb`** 的 **STM32F411CEU6** 高精度功率采集板。
- **核心能力**：
  - 三挡串行分流器电流采集（`1 µA ~ 5 A`：`LOW 50 Ω` / `MID 500 mΩ` / `HIGH 5 mΩ`，`INA190A2` ×3 + `REF3030` + `1V5_REF` 偏置）；
  - 四通道 24-bit 电压采集（`ADS1220IPWR`：`SENS4` 主母线 + `H1` `V_IN1..V_IN3`，`47 kΩ / 2.2 kΩ` 分压）；
  - 双备份 EEPROM 校准与 Welford 在线精度统计引擎（`BL24C02F`）；
  - 双串口架构：`H4 (USART1)` 对接 **ESP32 (`W:\mnt\project\lvgl-demo`)** 主控，`H3 (USART2)` 提供 115200 交互式三挡精度测试与校准终端。

---

## 2. 分层架构与关键入口

严格保持自底向上单向依赖，严禁上层直接操作底层寄存器：

| 层级 | 目录 | 职责与核心文件 |
| :--- | :--- | :--- |
| **Core** | `Core/` | 时钟与板级物理常量定义：[board_pins.h](Core/Inc/board_pins.h)、[main.c](Core/Src/main.c)、[stm32f4xx_it.c](Core/Src/stm32f4xx_it.c) |
| **BSP** | `BSP/` | STM32 外设驱动（DMA/TIM/SPI/I2C/UART/GPIO）：[bsp_gpio.c](BSP/Src/bsp_gpio.c)、[bsp_adc_dma.c](BSP/Src/bsp_adc_dma.c)、[bsp_spi_bus.c](BSP/Src/bsp_spi_bus.c)、[bsp_i2c_bus.c](BSP/Src/bsp_i2c_bus.c)、[bsp_uart.c](BSP/Src/bsp_uart.c) |
| **Driver** | `Driver/` | 器件驱动（换挡 MOSFET、ADS1220、24C02）：[drv_shunt_range.c](Driver/Src/drv_shunt_range.c)、[drv_ads1220.c](Driver/Src/drv_ads1220.c)、[drv_eeprom_24c02.c](Driver/Src/drv_eeprom_24c02.c) |
| **Service** | `Service/` | 平台无关算法与协议层（可在 Linux 主机直接单测）：[svc_sampler.c](Service/Src/svc_sampler.c)、[svc_calib_store.c](Service/Src/svc_calib_store.c)、[svc_host_proto.c](Service/Src/svc_host_proto.c) |
| **App** | `App/` | 主调度循环与 `H3` 诊断/测精度终端：[app_main.c](App/Src/app_main.c)、[app_diag_cli.c](App/Src/app_diag_cli.c) |
| **Tests** | `tests/` | 主机端算法、状态机、双备份 EEPROM 与串口协议回归测试：[test_sampler_and_proto.c](tests/test_sampler_and_proto.c) |

---

## 3. 编译与验证命令

### 3.1 固件交叉编译（Windows PowerShell + EIDE 工具链）
```powershell
$env:PATH = "C:\Users\kyrie\.eide\bin\builder\msys\bin;C:\Users\kyrie\.eide\tools\gcc_arm\bin;" + $env:PATH
& "C:\Users\kyrie\.eide\bin\builder\msys\bin\make.exe" -j8 TOOLCHAIN_DIR="C:/Users/kyrie/.eide/tools/gcc_arm/bin"
```
产物输出至 `build/make/output/power_sampler_f411.{elf,hex,bin}`。也可在 VS Code 中直接按 `Ctrl + Shift + B` 触发 [tasks.json](.vscode/tasks.json)。

### 3.2 主机单元测试（WSL Ubuntu-22.04 GCC）
修改 `Service/` 层的采样、换挡、校准或 `H4` 协议代码后，必须先运行主机单测（`-Wall -Wextra -Werror` 零警告）：
```powershell
wsl -d Ubuntu-22.04 -e bash -lc "cd /mnt/c/Users/kyrie/DeskBox/Folder/Power-Sampler-F411 && gcc -std=c11 -O2 -Wall -Wextra -Werror -DHOST_UNIT_TEST -ICore/Inc -IDriver/Inc -IService/Inc tests/test_sampler_and_proto.c Service/Src/svc_calib_store.c Service/Src/svc_sampler.c Service/Src/svc_host_proto.c -lm -o /tmp/test_ps && /tmp/test_ps"
```

---

## 4. 关键硬件与固件不变量（修改前必读）

未经核对原理图禁止改动以下硬件约束与工程约定：

1. **独立启动地址 `0x08000000`**：无 USB BootLoader 偏移（`SCB->VTOR = FLASH_BASE`），链接脚本 [STM32F411CEUx_FLASH.ld](STM32F411CEUx_FLASH.ld) 完整使用 512 KB Flash。
2. **上电防过流烧毁 `R16 (50 Ω)`**：`PB4` 复位默认为 `NJTRST`；`bsp_gpio_init()` 在切换 `PB4/PB5` 为推挽输出前必须先通过 `BSRR` 预置 `EN1=1, EN2=0`（锁住 `5 mΩ` 高流旁路），且 `DMA2` 中断中的 `fast_adc_guard_isr` 保持 `< 1 ms` 纯整数轨饱和直通保护。
3. **高边 PMOS 栅极 RC 消隐时间**：`Q1/Q2` 栅源极并联 `C21/C22 = 100 nF`，经 `R19/R20 = 100 kΩ` 关断泄放时间常数 $\tau \approx 10\text{ ms}$。必须保留 **升挡 `5 ms`、降挡 `45 ms`** 消隐（`SETTLE_CHUNKS_UPRANGE` / `SETTLE_CHUNKS_DOWNRANGE`），不得退回原开源 `Power-Pico` 的 `1 ms`。
4. **严禁引入原版分压漏流扣除**：`采样pcb` 的 `R2+R3 (49.2 kΩ)` 位于 `J1 (SENS4)` 分流链**上游**，不流过任何采样电阻；不得恢复 `Power-Pico` 的 `- V / 11.0` 扣除公式。
5. **ADS1220 四路单端与 SPI 模式**：`AIN0..3` 均单端参考 `AVSS`，必须配置 **`PGA_BYPASS = 1, Gain = 1`**（TI SBAS501），且 `SPI2` 必须使用 **Mode 1 (`CPOL = LOW, CPHA = 2EDGE`)**。
6. **`H4` 电源与 SWD 复位约定**：`H4`（1×8P）的 `Pin 1` 与 `Pin 8` 均为 **`3V3`**（**严禁接 `5V`**，`5V` 供电入口在 `H2.2`）；`H4.5..8` 为 4 线 SWD，**未引出 `NRST`**，烧录/调试必须使用 Software Reset。
7. **两点校准数学模型**：采用输入折算零偏模型 `I = (delta_lsb * scale + offset) * gain`，确保先 `cal zero` 再 `cal cur` 时，增益调整不破坏已标定的 `0 A` 零点。
8. **嵌入式安全编码红线**：零 `malloc`/`free`、零递归、中断中禁止浮点与阻塞操作、所有应用源码在 `-Wall -Wextra -Werror` 下保持零告警、不擅自修改原始参考仓库 `Power-Pico`。
