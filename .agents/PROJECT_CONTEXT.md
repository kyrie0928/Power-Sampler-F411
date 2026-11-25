# Power-Sampler-F411 项目上下文与维护记录

> 本文件用于长期沉淀硬件来源、已决策需求、基线验证状态与待推进事项，方便跨会话无缝接手。

---

## 1. 硬件来源与外部关联项目

- **嘉立创EDA（EasyEDA）工程**：`采样pcb`（Project UUID: `2d3471d84dc04bb4bfb6354d27dc521c`，板子 `Board1`，原理图 `P1.Schematic1` UUID: `0c1ff9976463e125`，PCB `PCB1` UUID: `81be9135dc8719c4`）。
  - 规模与检查结果：93 个器件、59 个网络、626 条走线、192 个过孔；`PCB1` 严格 DRC `0` 错误。
  - **注意**：通过 `easyeda-api` 连接时观察到 `P1.Schematic1` 存在未保存修改（`isUnSaved: 1`），后续若操作 EDA 文档切勿未经用户确认修改或关闭。
- **原开源参考仓库**：`c:\Users\kyrie\DeskBox\Folder\Power-Pico`（仅供对照硬件差异，禁止直接修改）。
- **关联主控项目**：`W:\mnt\project\lvgl-demo`（ESP32 主控，3.3 V UART2 与本板 `H4 / USART1` 对接；同样未修改其源码）。

---

## 2. 已确认的需求与设计决策

1. **仓库命名与工具链**：独立新建 Git 仓库 `c:\Users\kyrie\DeskBox\Folder\Power-Sampler-F411`；使用 **VS Code + EIDE / Make / CMake**（本机工具链位于 `C:\Users\kyrie\.eide\tools\gcc_arm\bin\arm-none-eabi-gcc.exe` 15.2.1 与 `C:\Users\kyrie\.eide\bin\builder\msys\bin\make.exe`）。
2. **`H1` 预留普通 IO（`PA8 / PA11 / PA12`）**：上电默认安全高阻输入，同时支持 `H3` CLI（`io cfg/set/read`）与 `H4` ESP32 串口协议（`0x20/0x21/0x22`）动态切换输入、上下拉、推挽或开漏输出。
3. **`H4 (USART1)` 对接 ESP32 (`lvgl-demo`)**：弃用原 `PowerPico_Client` PC USB 私有协议，改用轻量 `0xAA 0x55` + `CRC16-CCITT` 小端序二进制协议，输出整数定标物理量（`10 nA`、`1 µV`、`1 nW`、`1 µJ`）及 `48 B` 紧凑快照，支持轮询（`0x02`）与 `10~10000 ms` 定时推流（`0x60`）。
4. **烧录器约定**：用户实板计划使用 **ST-Link** 通过 `H4.5..8`（4 线 SWD，无 `NRST`，Software Reset）烧录。
   - `.vscode/launch.json` 已包含 `Debug (OpenOCD ST-Link SWD)`；
   - `.eide/eide.json` 当前 `uploader` 字段仍默认为 `JLink`，如用户希望 EIDE 图形按钮一键用 ST-Link 烧录，可按需改写为 `STLink`。
5. **相比原版 `Power-Pico` 的实核硬件修正**：
   - `25 MHz HSE -> 100 MHz SYSCLK`，`APB1 = 50 MHz` / `APB2 = 100 MHz` / `TIMCLK = 100 MHz` / `ADCCLK = 25 MHz`；
   - 修正 `ADC1` 扫描序列：移除悬空 `PA5 (IN5)`，采样周期提升至 `84 CYCLES`，消除采样保持电容向 `PA6 (LOW_CUR)` 的通道串扰；
   - 修正 `I2C1` `BL24C02` 为通用安全的 `8 B` 页写 + 复位 9 时钟总线恢复，实现 `0x00`/`0x80` 双备份互修；
   - 基于 `HAL` 库实际未打包 `hal_uart.c` 的现状，在 `bsp_uart.c` 采用轻量 CMSIS 寄存器 + 中断环形缓冲实现双串口，并严格按 `SR -> DR` 读序清除 `ORE`。

---

## 3. 基线提交与已验证状态

- **Git 提交记录**：
  - `e99291a`：`feat: initialize Power-Sampler-F411 firmware for redesigned 3-range sampler PCB`
  - `b515929`：`docs: fix H4 8-pin header pinout (3V3 + SWD) and H2 5V input`（按网表修正 `H4` 8 脚定义：`1=3V3, 2=HOST_RX, 3=HOST_TX, 4=GND, 5=SWDIO, 6=SWCLK, 7=GND, 8=3V3`，并补充 `H2` 5V 输入与 `H3` 线序）。
- **已验证项**（纯主机与交叉编译阶段）：
  - **主机单元测试（WSL GCC `-Wall -Wextra -Werror`）**：5 组测试全部通过（三挡换算与两点校准、`45 ms` 降挡消隐与自动换挡、Welford 精度统计、`BL24C02` 主备损坏自愈、`H4` ESP32 串口粘包/错包重同步与推流）。
  - **固件交叉编译（ARM GCC 15.2.1 `-Wall -Wextra -Werror`）**：零告警通过，生成 `.elf / .hex / .bin`（`FLASH: 58,392 B / 11.14%`，`RAM: 27,024 B / 20.62%` 含 16 KB 保留栈）。
- **尚未验证项**：**尚未烧录至物理开发板/采样板进行上电与实测验证。**

---

## 4. 下一阶段待办与上板检查清单

1. **ST-Link 首次上板与电源检查**：
   - 使用 ST-Link 接 `H4.5..8`（若 `H2` 已供 `5V` 则只接 `SWDIO/SWCLK/GND`）烧录，确认 `PC13 (LED1)` 正常 `500 ms` 闪烁（`ADS1220` 在线前/异常时为 `100 ms` 快闪）。
   - 实测板上 `VDD (U10 BL8039 -> 3.3 V)` 与 `VDDA (U6 REF3030 -> 3.000 V)` 压差（接近 STM32F411 数据手册 `|VDD - VDDA| <= 300 mV` 边界），通过 `H3` 执行 `status` 检查 `1V5_REF (PB1)` 原始码是否稳定在 `2048 ± 10 LSB`。
2. **三挡精度实测与 EEPROM 校准（`H3 / USART2` 115200）**：
   - 按 `README.md` 第 3 节 SOP，在 `J2` 开路下依次完成 `low / mid / high` `cal zero`，再用标准源/六位半万用表完成 `cal cur`、`cal vol`、`prec 2000` 噪声统计评估并 `cal save` 掉电重上电复核。
3. **动态负载阶跃换挡示波器复测**：
   - 在 `range auto` 下测试 `400 µA ↔ 2 mA` 与 `40 mA ↔ 80 mA` 跳变，确认 `45 ms` 降挡消隐与 `< 1 ms` DMA 紧急保护无震荡、无误跳挡；注意输入共模电压勿超过 `INA190` `40 V` 耐压上限。
4. **可选 EIDE 烧录器切换**：如用户需要，在 `.eide/eide.json` 将默认 `uploader` 由 `JLink` 调整为 `STLink`。
5. **ESP32 (`lvgl-demo`) 联调接入**：将 `README.md` 第 4 节的 `48 B` 帧解析器接入 `W:\mnt\project\lvgl-demo` 串口屏幕界面。

---

## 5. 推荐技能（Suggested Skills）

- `easyeda-api`：需要继续核对或查看嘉立创EDA `采样pcb` 原理图/PCB 网络或器件参数时使用。
- `diagnosing-bugs`：物理回板测试中遇到 `ADS1220` SPI 通信异常、换挡震荡、串口丢帧或底噪超标时使用。
- `code-change-self-review`：修改驱动、换挡状态机或协议代码后执行自审。
- `tdd`：扩展或调整 `Service/` 层换挡阈值、校准算法与协议指令时先补主机单测再改实现。
- `project-onboarding-agents`：后续有重大模块变更时同步维护本记录与 `AGENTS.md`。
