# Power-Sampler-F411 (`采样pcb` 三挡高精度功率采集固件)

基于 **STM32F411CEU6 + INA190A2 ×3 + ADS1220 (24-bit) + REF3030 + BL24C02** 的独立高精度三挡位电流/四通道电压功率采集板固件，专为改版硬件 **`采样pcb`** 设计，提供：

1. **三挡位宽动态电流采集（`1 µA ~ 5 A`）与自动/手动锁挡测量**
2. **四通道 24-bit 电压采集（`SENS4` 主母线 + `H1` 扩展 `V_IN1..V_IN3`）**
3. **板载 Welford 在线均值/标准差（$\sigma$）/峰峰值统计引擎与双备份 EEPROM 校准**（`H3 / USART2` 交互终端）
4. **面向 ESP32 (`lvgl-demo`) 主控的轻量二进制 CRC16 采集与控制协议**（`H4 / USART1`）

---

## 1. 硬件引脚与外设映射（对应嘉立创EDA `采样pcb`）

| 模块 | MCU 引脚 | 原理图网络 / 器件 | 功能说明 |
| :--- | :--- | :--- | :--- |
| **系统时钟** | `PH0` / `PH1` | `U3` 25 MHz 无源晶振 | PLL 倍频至 `100 MHz SYSCLK`（`APB1=50MHz`，`APB2=100MHz`） |
| **状态灯** | `PC13` | `LED1`（高电平点亮） | 正常 `500 ms` 闪烁；过流或 `ADS1220` 离线 `100 ms` 快闪 |
| **量程控制** | `PB4` (`EN1`), `PB5` (`EN2`) | `Q3/Q4` → `Q2/Q1` (`AP30P30Q`) | `EN1=1,EN2=0`：`HIGH` (`5 mΩ`)；`EN1=0,EN2=1`：`MID` (`500 mΩ`)；`0,0`：`LOW` (`50 Ω`) |
| **电流 ADC** | `PA6` / `PA7` / `PB0` / `PB1` | `ADC1_IN6..IN9` + `TIM2` 10kHz DMA | `IN6=IADC3(50Ω)`, `IN7=IADC2(500mΩ)`, `IN8=IADC1(5mΩ)`, `IN9=1V5_REF` |
| **24位电压 ADC** | `PB12..PB15`, `PA0` | `U2 ADS1220IPWR` (`SPI2` Mode 1 + `EXTI0`) | `AIN0=SENS4` 主输入, `AIN1..3=V_IN1..3`（`47k/2.2k` 0.1% 分压，`PGA_BYPASS=1`） |
| **校准存储** | `PB6` (`SCL`), `PB7` (`SDA`) | `U4 BL24C02F` (`I2C1`, `0x50`) | `0x00` 主分区 + `0x80` 备份分区双副本 CRC16 存储 |
| **主控通信口 (`H4.2..3`)** | `PA9` (`TX`), `PA10` (`RX`) | `USART1` (`115200 8N1`) | 经 `R29/R30 (100 Ω)` 对接 **ESP32 (`lvgl-demo`)**，与 SWD 复用 8P 排针 `H4` |
| **SWD 烧录调试口 (`H4.5..8`)** | `PA13` (`SWDIO`), `PA14` (`SWCLK`) | `H4.5~8` (`SWDIO/SWCLK/GND/3V3`) | 4 线 SWD 接口（ST-Link V2/V3），未引出 `NRST` |
| **调试/测精度口 (`H3`)** | `PA2` (`TX`), `PA3` (`RX`) | `USART2` (`115200 8N1`) | 经 `R31/R34 (100 Ω)` 接 `H3`，交互式三挡精度统计与校准 CLI |
| **5V 供电输入 (`H2`)** | — | `H2.1=GND`, `H2.2=VIN5V` | 经 `U10 (BL8039)` 降压生成系统 `3V3` |
| **预留扩展 IO (`H1`)** | `PA8`, `PA11`, `PA12` | `H1.6`, `H1.5`, `H1.4` | 默认高阻输入，支持通过 CLI 或 ESP32 协议动态配置输入/上下拉/推挽/开漏 |

### 相对原开源 `Power-Pico` 的关键工程修正
- **移除 `0x08010000` USB BootLoader 偏移**：链接脚本从 `0x08000000` 完整使用 512 KB Flash，SWD 直接烧录即运行。
- **高边 PMOS 栅极 RC 消隐保护**：`Q1/Q2` 栅源极并联 `C21/C22 = 100 nF`，经 `R19/R20 = 100 kΩ` 关断泄放时间常数 $\tau \approx 10\text{ ms}$；固件在升挡时消隐 `5 ms`、降挡时消隐 `45 ms`，并在 `DMA2` 中断内置 `<1 ms` 硬件过流直通高挡保护，防止 `50 Ω` 采样电阻过载或连跳误判。
- **移除错误的下游分压漏流扣除**：`采样pcb` 的 `R2+R3 (49.2 kΩ)` 位于 `J1 (SENS4)` 分流器上游，不流过 `R16~R18`，不再错误扣除 `V/1.1MΩ`，保障微安挡零点准确度。
- **ADS1220 四路单端合规配置**：针对 `AINx - AVSS` 单端接地输入，严格遵循 TI SBAS501 数据手册置位 `PGA_BYPASS=1, Gain=1`，SPI2 修正为 Mode 1 (`CPOL=0, CPHA=2EDGE`)。

---

## 2. 编译、单元测试与烧录

### 2.1 VS Code + EIDE / GNU Make 编译
项目已内置 `Power-Sampler-F411.code-workspace`、`.eide/eide.yml`、`.vscode/tasks.json`、`.vscode/launch.json` 与 `Makefile`：
- **EIDE 侧边栏构建**（推荐，配合 EIDE 一键烧录）：打开 `Power-Sampler-F411.code-workspace` 后点击 EIDE 的 **Build**，产物位于 `build/eide/Release/Power-Sampler-F411.{hex,bin,elf}`。
- **Make / `Ctrl + Shift + B` 构建**（配合 `F5` 单步调试）：
```powershell
& "C:\Users\kyrie\.eide\bin\builder\msys\bin\make.exe" -j8 TOOLCHAIN_DIR="C:/Users/kyrie/.eide/tools/gcc_arm/bin"
```
产物位于 `build/make/output/power_sampler_f411.{hex,bin,elf}`。

| 固件格式 | 是否含烧录地址 | 适用场景与注意 |
| :--- | :--- | :--- |
| **`.hex`**（推荐） | **自带地址 `0x08000000`** 与校验 | EIDE 默认烧录格式；无需手填地址，不易烧错偏移 |
| **`.bin`** | **不含地址**（纯二进制） | 任何工具烧录 `.bin` 都**必须手动指定起始地址 `0x08000000`** |
| **`.elf`** | 含地址与源码调试符号 | 供 VS Code `F5`（OpenOCD + GDB）单步调试使用，也可直接给 OpenOCD 烧录 |

### 2.2 CMake 编译（可选）
```powershell
cmake --preset Release
cmake --build --preset Release
```

### 2.3 主机端算法与协议单元测试（WSL / Linux GCC）
```bash
gcc -std=c11 -O2 -Wall -Wextra -Werror -DHOST_UNIT_TEST \
    -ICore/Inc -IDriver/Inc -IService/Inc \
    tests/test_sampler_and_proto.c \
    Service/Src/svc_calib_store.c \
    Service/Src/svc_sampler.c \
    Service/Src/svc_host_proto.c \
    -lm -o /tmp/test_ps && /tmp/test_ps
```

### 2.4 ST-Link SWD 烧录与调试
板上 **`H4`（1x8P 2.54 mm 排针）的 Pin 5~8** 即为 4 线 SWD 接口，本工程统一使用 **ST-Link（V2 / V3）+ OpenOCD**：
- `H4 Pin 5`：`SWDIO` (`PA13`)
- `H4 Pin 6`：`SWCLK` (`PA14`)
- `H4 Pin 7`：`GND`
- `H4 Pin 8`：`3V3`（仅接 3.3V，**严禁接 5V**；板子已从 `H2` 供 5V 时可不接此脚）

> [!NOTE]
> `H4` 未引出 `NRST` 复位引脚，烧录与调试请保持默认的 **Software Reset**（不要勾选 Connect Under Reset / Hardware Reset）。

#### 方式 1：VS Code EIDE 一键烧录（默认 `.hex`，推荐）
1. 在 VS Code 打开根目录的 `Power-Sampler-F411.code-workspace`（首次使用请在 EIDE `Operations → Setup Utility Tools` 安装 **`OpenOCD`**）；
2. 点击 EIDE 面板的 **Build** 生成 `build/eide/Release/Power-Sampler-F411.hex`，再点击 **Program Flash**；
3. EIDE 按 `.eide/eide.yml` 自动调用 OpenOCD 烧录 **`.hex`** 并执行校验与复位运行；如需改烧 **`.bin`**，在 EIDE `Flasher Configurations : OpenOCD` 中指定 `bin` 路径并保持 `baseAddr` 为 `0x08000000`。

#### 方式 2：OpenOCD 命令行烧录（`.hex` 与 `.bin` 命令对比）
> 如使用 Make 编译产物，只需把下面路径中的 `build/eide/Release/Power-Sampler-F411.*` 替换为 `build/make/output/power_sampler_f411.*` 即可。

- **烧录 `.hex`（自带地址，不写偏移）**：
```powershell
$openocd = (Get-Item "$env:USERPROFILE\.eide\tools\openocd*\bin\openocd.exe" | Select-Object -First 1).FullName
& $openocd -f interface/stlink.cfg -f target/stm32f4x.cfg `
  -c "program build/eide/Release/Power-Sampler-F411.hex verify reset exit"
```

- **烧录 `.bin`（不含地址，必须显式加上 `0x08000000`）**：
```powershell
$openocd = (Get-Item "$env:USERPROFILE\.eide\tools\openocd*\bin\openocd.exe" | Select-Object -First 1).FullName
& $openocd -f interface/stlink.cfg -f target/stm32f4x.cfg `
  -c "program build/eide/Release/Power-Sampler-F411.bin 0x08000000 verify reset exit"
```

#### 方式 3：STM32CubeProgrammer（图形界面 / CLI，适合无 VS Code 电脑或产线）
- **图形界面（GUI）**：连接方式选 **`SWD`**，Mode 选 **`Normal`**，Reset Mode 选 **`Software system reset`**：
  - 烧录 **`.hex`**：打开 `.hex` 文件后直接点 `Start Programming`（自动识别 `0x08000000`）；
  - 烧录 **`.bin`**：打开 `.bin` 文件后，**必须确认 `Start address` 填写为 `0x08000000`** 再点 `Start Programming`。
- **命令行（`STM32_Programmer_CLI`）**：
```powershell
# 烧录 .hex（自带地址）
STM32_Programmer_CLI -c port=SWD reset=SWrst -w build/eide/Release/Power-Sampler-F411.hex -v -rst

# 烧录 .bin（必须带 0x08000000）
STM32_Programmer_CLI -c port=SWD reset=SWrst -w build/eide/Release/Power-Sampler-F411.bin 0x08000000 -v -rst
```

#### 方式 4：VS Code `F5` 在线单步调试（自动下载 `.elf`）
先按 `Ctrl + Shift + B` 生成 `build/make/output/power_sampler_f411.elf`，再按 `F5` 启动 `.vscode/launch.json` 中的 `Debug (OpenOCD ST-Link SWD)`。

---

## 3. 三挡电流与四路电压精度实测 & 校准流程（`H3 / USART2`）

将 USB-TTL（`3.3V` 电平）连接至 **`H3`（1x3P 2.54 mm 排针）**：
- `H3 Pin 1 (LOG_HOST_RX)`：经 `R31 (100 Ω)` 连 `PA2 (USART2_TX)` → 接 USB-TTL `RX`
- `H3 Pin 2 (LOG_HOST_TX)`：经 `R34 (100 Ω)` 连 `PA3 (USART2_RX)` → 接 USB-TTL `TX`
- `H3 Pin 3 (GND)` → USB-TTL `GND`
- 波特率：**`115200 8N1`**，行尾 `CR/LF`

### 3.1 分挡零点清零（负载端 `J2` 悬空开路）
```text
range low
cal zero
range mid
cal zero
range high
cal zero
```

### 3.2 分挡满度/典型点增益校准（串联六位半万用表或标准源）
1. **低流挡 (`LOW_50R`, `1 ~ 500 µA`)** —— 例如通入约 `400 µA`，实测万用表读数 `399.82 µA`：
   ```text
   range low
   cal cur low 399.82
   prec 2000
   ```
2. **中流挡 (`MID_500mR`, `0.5 ~ 50 mA`)** —— 例如通入约 `40 mA`（`40000 µA`），万用表读数 `40015.0 µA`：
   ```text
   range mid
   cal cur mid 40015.0
   prec 2000
   ```
3. **高流挡 (`HIGH_5mR`, `50 mA ~ 5 A`)** —— 例如通入约 `2 A`（`2000000 µA`），万用表读数 `1998500 µA`：
   ```text
   range high
   cal cur high 1998500
   prec 2000
   ```
4. **四路 24-bit 电压校准（`0=SENS4`, `1=V_IN1`, `2=V_IN2`, `3=V_IN3`）** —— 例如输入 `12000.0 mV`：
   ```text
   vmode prec20
   cal vol 0 12000.0
   prec 2000
   vmode scan
   ```
5. **写入 EEPROM 持久化并恢复自动换挡**：
   ```text
   range auto
   cal save
   cal show
   ```

> `prec 2000` 会采集 `2000 ms`（`20,000` 个 10 kHz 电流点 + `ADS1220` 电压点），输出均值 `Mean`、标准差 `StdDev`、峰峰值 `Pk-Pk` 以及底层 `ADC12 / 1V5_REF / ADS24` 原始码均值，可直接量化评估每个挡位的底噪与线性度。

---

## 4. ESP32 (`lvgl-demo`) 接线与 `H4 (USART1)` 通信协议

### 4.1 物理接线（`H4` 8P 排针：3.3V 串口 + SWD 复用 & `H2` 5V 供电）

> [!CAUTION]
> **`H4 Pin 1` 与 `H4 Pin 8` 均为 `3V3` 网络！切勿将 `5V` 接到 `H4`，否则会直接损坏 STM32 / ADS1220。** 整板 5V 供电输入请接入 **`H2`**（`H2 Pin 1 = GND`，`H2 Pin 2 = VIN5V`）。

| 排针序号 | 原理图网络 | STM32 引脚 / 连接 | 外部对接（ESP32 / 烧录器） |
| :--- | :--- | :--- | :--- |
| `H4 Pin 1` | `3V3` | 系统 `3.3 V` 电源轨 | 仅用于 `3.3 V` 逻辑参考或供电（**严禁接 5V**） |
| `H4 Pin 2` | `HOST_RX` | 经 `R29 (100 Ω)` 连 `PA9 (USART1_TX)` | 接 **ESP32 `RX`**（如 `GPIO16`） |
| `H4 Pin 3` | `HOST_TX` | 经 `R30 (100 Ω)` 连 `PA10 (USART1_RX)` | 接 **ESP32 `TX`**（如 `GPIO17`） |
| `H4 Pin 4` | `GND` | `GND` | 接 **ESP32 `GND`** |
| `H4 Pin 5` | `SWDIO` | `PA13` | 接 **ST-Link `SWDIO`** |
| `H4 Pin 6` | `SWCLK` | `PA14` | 接 **ST-Link `SWCLK`** |
| `H4 Pin 7` | `GND` | `GND` | 接 **ST-Link `GND`** |
| `H4 Pin 8` | `3V3` | 系统 `3.3 V` 电源轨 | 接 **ST-Link `3V3`**（与板子供电共电时注意避免电源冲突） |

> 5V 主供电接入 **`H2`（1x2P 排针）**：`H2 Pin 1 = GND`，`H2 Pin 2 = VIN5V`（经 `U10 BL8039` 降压产生 `3V3`）。

串口参数：**`115200 8N1`**，小端序（Little-Endian）。

### 4.2 二进制帧格式
| 偏移 | 字段 | 长度 | 说明 |
| :--- | :--- | :--- | :--- |
| `0..1` | `SOF` | 2 B | 固定帧头 `0xAA 0x55` |
| `2` | `ver` | 1 B | 协议版本 `0x01` |
| `3` | `cmd` | 1 B | 请求命令码；应答帧为 `cmd \| 0x80`；主动推流帧为 `0x60` |
| `4..5` | `seq` | 2 B | `uint16_t` 递增序号 |
| `6..7` | `len` | 2 B | `uint16_t` 载荷字节数（`0..80`） |
| `8..8+len-1` | `payload` | `len` B | 应答帧首字节 `payload[0]` 为状态码（`0x00 = OK`），其后为数据体 |
| `末尾 2B` | `crc16` | 2 B | `CRC16-CCITT`（多项式 `0x1021`，初值 `0xFFFF`，覆盖 `[ver .. payload末尾]`） |

### 4.3 快照数据结构 `proto_snapshot_payload_t`（共 48 字节，`#pragma pack(1)`）
`PROTO_CMD_GET_SNAPSHOT (0x02)` 应答数据体（`payload[1..48]`）及 `PROTO_EVT_STREAM_SNAP (0x60)` 推流载荷（`payload[0..47]`）完全一致：
```c
#pragma pack(push, 1)
typedef struct {
    uint32_t uptime_ms;      /* 运行毫秒数 */
    uint8_t  status_flags;   /* bit0=ADS在线, bit1=EEPROM正常, bit2=已加载校准, bit3=过流告警, bit4=换挡消隐中, bit5=窗口有效 */
    uint8_t  range_mode;     /* 0=AUTO, 1=LOCK_LOW, 2=LOCK_MID, 3=LOCK_HIGH */
    uint8_t  active_range;   /* 当前生效挡位: 1=LOW(50R), 2=MID(500mR), 3=HIGH(5mR) */
    uint8_t  ext_io_mask;    /* H1 预留IO电平: bit0=PA8, bit1=PA11, bit2=PA12 */
    int64_t  current_10na;   /* 电流，单位 10 nA (0.01 uA)，例如 5205 = 52.05 uA */
    int32_t  v_main_uv;      /* SENS4 主输入电压，单位 1 uV */
    int32_t  v_ext_uv[3];    /* H1 V_IN1..3 电压，单位 1 uV */
    int64_t  power_nw;       /* 主回路功率，单位 1 nW */
    int64_t  energy_uj;      /* 累计能量，单位 1 uJ */
} proto_snapshot_payload_t;
#pragma pack(pop)
```

### 4.4 命令列表
| 命令码 | 名称 | 请求 Payload | 应答数据体（`status=0x00` 后） |
| :--- | :--- | :--- | :--- |
| `0x01` | `PING` | 空 (`0B`) | `ver_major(1B), ver_minor(1B), ver_patch(1B), status_flags(1B), uptime_ms(4B)` |
| `0x02` | `GET_SNAPSHOT` | 空 (`0B`) | `proto_snapshot_payload_t` (`48B`) |
| `0x03` | `SET_RANGE` | `mode(1B)`: `0=AUTO, 1=LOW, 2=MID, 3=HIGH` | 当前 `mode(1B)` |
| `0x04` | `SET_STREAM` | `period_ms(uint16)`: `0=关闭`, 或 `10..10000` ms 定时推送 `0x60` 帧 | 当前 `period_ms(2B)` |
| `0x05` | `SET_ADS_MODE` | `ads_mode(1B)`: `0=SCAN_4CH, 1=FAST_600SPS, 2=PREC_20SPS` | 当前 `ads_mode(1B)` |
| `0x06` | `CLEAR_ENERGY` | 空 (`0B`) | 空 |
| `0x10` | `CAL_GET` | 空 (`0B`) | 3组电流 `(gain, offset_ua)` + 4组电压 `(gain, offset_mv)` 共 `56B float` |
| `0x11` | `CAL_SET_CUR` | `range(1..3), gain(f32), offset_ua(f32), save_nv(0/1)` (`10B`) | 空 |
| `0x12` | `CAL_SET_VOL` | `ch(0..3), gain(f32), offset_mv(f32), save_nv(0/1)` (`10B`) | 空 |
| `0x13` | `CAL_RESET` | 确认字 `0xC3 0x3C` (`2B`) | 空 |
| `0x20` | `EXT_IO_CFG` | `id(0..2), mode(0..4), init_high(0/1)` (`3B`) | 空 |
| `0x21` | `EXT_IO_WRITE` | `id(0..2), high(0/1)` (`2B`) | 空 |
| `0x22` | `EXT_IO_READ` | 空 (`0B`) | `mask(1B), mode0(1B), mode1(1B), mode2(1B)` (`4B`) |
