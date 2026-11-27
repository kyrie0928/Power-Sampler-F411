# Power-Sampler-F411

基于 **STM32F411CEU6 + INA190A2 ×3 + ADS1220 (24-bit) + REF3030 + BL24C02** 的三挡高精度功率采集板固件（对应嘉立创EDA工程 **`采样pcb`**）：

- **三挡电流采集（`1 µA ~ 5 A`）**：`50 Ω / 500 mΩ / 5 mΩ` 自动切换（升挡 5 ms / 降挡 45 ms 消隐 + `<1 ms` DMA 过流保护）与手动锁挡。
- **四路 24-bit 电压采集**：`SENS4` 主母线 + `H1` 扩展 `V_IN1..3`（`47 kΩ / 2.2 kΩ` 分压，`PGA_BYPASS=1`）。
- **校准与双界面**：`BL24C02` 双备份校准、`H3` 调试校准串口、`H4` 对接 ESP32 (`lvgl-demo`) 二进制协议。
- **版本管理**：修改 `Core/Inc/fw_version.h` 中的 `FW_VERSION_MAJOR/MINOR/PATCH` 即可同步更新串口显示、通信 `PING` 与固件文件名版本号。

---

## 1. 硬件接口与安全注意

> [!CAUTION]
> - **`H4 Pin 1` 与 `H4 Pin 8` 为 `3V3`（严禁接 `5V`）**；整板 5V 供电请接 **`H2`**（`H2.1=GND, H2.2=VIN5V`）。
> - **`H4` 未引出 `NRST`**，ST-Link 烧录与调试请保持 **Software Reset**（勿选 Connect Under Reset）。

| 接口 / 模块 | MCU 引脚 | 说明 |
| :--- | :--- | :--- |
| **系统时钟 / LED1** | `PH0/PH1` (25MHz→100MHz), `PC13` | `LED1` 上电常亮，H3/H4 串口收发数据时自动闪烁提示 |
| **量程控制 / 电流 ADC** | `PB4(EN1), PB5(EN2)` / `PA6, PA7, PB0, PB1` | `10 kHz` DMA；`LOW=50Ω`, `MID=500mΩ`, `HIGH=5mΩ` + `1V5_REF` |
| **24-bit 电压 / EEPROM** | `PB12..15, PA0` (`SPI2`) / `PB6, PB7` (`I2C1`) | `ADS1220`（Mode 1）+ `BL24C02`（`0x50`，`0x00/0x80` 双备份） |
| **`H3` 调试校准口** | `PA2(TX), PA3(RX)` (`USART2`, `115200 8N1`) | `H3.1→USB-TTL RX`，`H3.2→TX`，`H3.3→GND` |
| **`H4` 主控通信 + SWD** | `PA9/PA10` (`USART1`), `PA13/PA14` (`SWD`) | `1=3V3, 2=HOST_RX, 3=HOST_TX, 4=GND, 5=SWDIO, 6=SWCLK, 7=GND, 8=3V3` |
| **`H1` 扩展 IO** | `PA8(0), PA11(1), PA12(2)` | 上电高阻输入，支持 CLI / ESP32 协议动态配置输入/上下拉/推挽/开漏 |

---

## 2. 编译与 ST-Link 烧录

### 2.1 编译固件
- **EIDE（推荐，配合一键烧录）**：在 VS Code 打开 `Power-Sampler-F411.code-workspace` → 点击 EIDE **Build**，产物位于 `build/eide/Release/`。
- **Make（`Ctrl + Shift + B`，配合 `F5` 调试）**：产物位于 `build/make/output/`。
- 编译后会自动生成带版本号文件（如 `Power-Sampler-F411_0.0.1.hex/.bin`）并清理旧版本文件；同时保留原名文件供一键烧录与调试。
- **主机单元测试（WSL GCC）**：
  ```bash
  gcc -std=c11 -O2 -Wall -Wextra -Werror -DHOST_UNIT_TEST -ICore/Inc -IDriver/Inc -IService/Inc tests/test_sampler_and_proto.c Service/Src/svc_calib_store.c Service/Src/svc_sampler.c Service/Src/svc_host_proto.c -lm -o /tmp/test_ps && /tmp/test_ps
  ```

### 2.2 `.hex` / `.bin` / `.elf` 区别与 ST-Link 烧录方式

| 格式 | 是否含地址 | 适用场景与注意 |
| :--- | :--- | :--- |
| **`.hex`**（推荐） | **自带 `0x08000000`** | EIDE 一键烧录默认格式，无需手填地址 |
| **`.bin`** | **不含地址**（纯二进制） | 任何工具烧录 `.bin` **必须显式填写起始地址 `0x08000000`** |
| **`.elf`** | 含地址与调试符号 | 用于 VS Code `F5`（`Debug (OpenOCD ST-Link SWD)`）单步调试 |

1. **VS Code EIDE 一键烧录（默认 `.hex`）**：首次在 EIDE `Operations → Setup Utility Tools` 安装 `OpenOCD`，点击 **Build → Program Flash** 即可；如需改烧 `.bin`，在 `Flasher Configurations : OpenOCD` 指定 `bin` 路径并保持 `baseAddr=0x08000000`。
2. **OpenOCD 命令行（`.hex` 与 `.bin` 对比）**：
   ```powershell
   $openocd = (Get-Item "$env:USERPROFILE\.eide\tools\openocd*\bin\openocd.exe" | Select-Object -First 1).FullName
   # 烧录 .hex（自带地址）
   & $openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program build/eide/Release/Power-Sampler-F411.hex verify reset exit"
   # 烧录 .bin（必须带 0x08000000）
   & $openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program build/eide/Release/Power-Sampler-F411.bin 0x08000000 verify reset exit"
   ```
3. **STM32CubeProgrammer（GUI / CLI）**：接口选 `SWD`、Reset Mode 选 `Software system reset`（`.hex` 直接烧，`.bin` 必须填地址 `0x08000000`）：
   ```powershell
   STM32_Programmer_CLI -c port=SWD reset=SWrst -w build/eide/Release/Power-Sampler-F411.hex -v -rst
   STM32_Programmer_CLI -c port=SWD reset=SWrst -w build/eide/Release/Power-Sampler-F411.bin 0x08000000 -v -rst
   ```

---

## 3. 串口调试与校准（`H3 / USART2`, `115200 8N1`, `CR/LF`）

- **常用查看命令**：`ver`（版本）、`status`（完整快照与状态标志）、`watch 500`（定时打印，`watch off` 停止）、`prec 2000`（2 秒精度与底噪统计）、`range <auto|low|mid|high>`、`io read/cfg/set`。
- **标准校准流程**：
  1. 负载开路清零：依次锁挡 `range low/mid/high`，每挡执行 `cal zero`；
  2. 串联万用表增益校准：分别在对应挡位通入典型电流执行 `cal cur <low|mid|high> <实测uA>`；
  3. 电压校准：`vmode prec20` → `cal vol <0..3> <实测mV>` → `vmode scan`；
  4. 持久化：`range auto` → `cal save` → `cal show`。

---

## 4. ESP32 (`lvgl-demo`) 通信协议（`H4 / USART1`, `115200 8N1`, 小端序）

- **帧格式**：`SOF(0xAA 0x55)` + `ver(0x01)` + `cmd(请求 / cmd|0x80应答 / 0x60推流)` + `seq(u16)` + `len(u16, 0..80)` + `payload(应答首字节为status)` + `CRC16-CCITT(u16, 0x1021, 0xFFFF, 覆盖ver..payload末)`。
- **快照载荷 `proto_snapshot_payload_t`（`48 B`，`#pragma pack(1)`）**：
  `uptime_ms(u32)`, `status_flags(u8)`, `range_mode(u8)`, `active_range(u8)`, `ext_io_mask(u8)`, `current_10na(i64)`, `v_main_uv(i32)`, `v_ext_uv[3](i32×3)`, `power_nw(i64)`, `energy_uj(i64)`。
- **核心命令**：`0x01 PING`、`0x02 GET_SNAPSHOT`、`0x03 SET_RANGE`、`0x04 SET_STREAM(0或10..10000ms)`、`0x05 SET_ADS_MODE`、`0x06 CLEAR_ENERGY`、`0x10..0x13 CAL_GET/SET_CUR/SET_VOL/RESET`、`0x20..0x22 EXT_IO_CFG/WRITE/READ`。

---

## 5. 版本更新记录

### 0.0.1（2026-11-27，commit `b2f77dffec90f7444b0f03da8d0dab124c5c87f1`）

**新增功能**
- 新增统一固件版本号管理（`Core/Inc/fw_version.h`），串口和 ESP32 协议均可查询当前版本。
- 编译后自动生成带版本号的 `.hex` / `.bin` 固件，并清理旧版本产物。

**功能变更**
- `LED1` 改为上电常亮，串口收发数据时自动闪烁提示。
