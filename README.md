# Anbo_L4s5_App

Lightweight async microkernel for the **B-L4S5I-IOT01A** development board (STM32L4S5VITx, Cortex-M4F).

## Features

- C99, fully static memory (no `malloc`, no `sprintf`)
- Event bus (pub/sub), software timers, event-driven FSM
- **Fixed-size block pool + async event pointer queue** (zero-copy ISR→main dispatch)
- **Instrumentation trace hooks** (compile-time switchable, zero overhead when off)
- Async ring-buffer log system (zero-stdio formatting)
- ISR-driven USART1 device driver (ST-LINK VCP)
- Software watchdog (multi-slot: main + per-module) + IWDG hardware watchdog
- Stop 2 low-power idle with LPTIM1 wakeup + tick compensation
- **User-triggered deep sleep** (long-press 3 s; wake sources: UART RX / RTC / button / LPTIM / IMU vibration; configurable IWDG strategy)
- SRAM2 (ECC, Standby-retained) kernel pool placement
- **Persistent NVM configuration** (internal Flash or external OSPI NOR, multi-page wear-levelling rotation)
- **Business-level FaultManager** — fault registration, reporting, auto-recovery scanning, FSM integration
- **Temperature alarm demo** — ADC sensor → Pool async events → FSM controller (Normal/Alarm/Fault) → LED/UART output
- **IMU vibration detection** — LSM6DSL 6-axis sensor via I2C2, FIFO readout, software vibration thresholding, hardware wake-up from deep sleep via INT1

## Quick Start

Clone the repository and run the build script for your platform. **No manual tool installation required** — all dependencies (CMake, Ninja, ARM toolchain, STM32 HAL SDK) are downloaded automatically on first build.

### Windows (CMD)

```
git clone --recursive <repo-url>
cd Anbo_L4s5_App
build.bat
```

### Windows (PowerShell)

```powershell
git clone --recursive <repo-url>
cd Anbo_L4s5_App
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

### WSL / Linux / macOS

```bash
git clone --recursive <repo-url>
cd Anbo_L4s5_App
chmod +x build.sh
./build.sh
```

> If you already cloned without `--recursive`, run
> `git submodule update --init --recursive` to fetch the kernel.

> **First build** takes 5–10 minutes (downloads ~400 MB of tools). Subsequent builds complete in seconds.

## Build Options

| Option | CMD | PowerShell | Bash |
|--------|-----|------------|------|
| Debug build (default) | `build.bat` | `.\build.ps1` | `./build.sh` |
| Release build | `build.bat release` | `.\build.ps1 -Release` | `./build.sh release` |
| Clean rebuild | `build.bat clean` | `.\build.ps1 -Clean` | `./build.sh clean` |
| Set version | `build.bat version 1.2.3` | `.\build.ps1 -Version 1.2.3` | `./build.sh version 1.2.3` |
| All combined | `build.bat clean release version 1.2.3` | `.\build.ps1 -Clean -Release -Version 1.2.3` | `./build.sh clean release version 1.2.3` |

## Manual CMake Build

If you prefer to use your own toolchain:

```bash
cmake -B build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DFW_VERSION=1.2.3
cmake --build build
```

To point to a specific ARM toolchain installation:

```bash
cmake -B build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
    -DARM_TOOLCHAIN_PATH=/path/to/arm-none-eabi/bin
cmake --build build
```

## Auto-Downloaded Dependencies

All downloaded to `tools/` (Windows) or system-detected (Linux/WSL):

| Component | Version | Source |
|-----------|---------|--------|
| CMake | 4.0.1 | github.com/Kitware/CMake |
| Ninja | 1.12.1 | github.com/ninja-build/ninja |
| ARM GNU Toolchain | 13.3.rel1 | developer.arm.com |
| STM32L4xx HAL Driver | 1.13.5 | github.com/STMicroelectronics |
| CMSIS Device L4 | 1.7.3 | github.com/STMicroelectronics |
| CMSIS 5 Core | 5.9.0 | github.com/ARM-software |

> `tools/` and SDK directories are listed in `.gitignore` and are never committed.

## Output

After a successful build, the following files are generated in `build/`:

| File | Description |
|------|-------------|
| `l4s5_anbo_x.y.z.elf` | ELF executable (with debug symbols) |
| `l4s5_anbo_x.y.z.bin` | Raw binary (for flashing) |
| `l4s5_anbo_x.y.z.hex` | Intel HEX (for ST-LINK) |
| `l4s5_anbo_x.y.z.map` | Linker map file |

> Version `x.y.z` defaults to `0.1.0` (from `project(VERSION ...)`) if not specified.
> Override with `-DFW_VERSION=1.2.3` or via build script `version` parameter.

## Flashing

Connect the B-L4S5I-IOT01A board via USB (ST-LINK), then:

### GUI (STM32CubeProgrammer)

1. Open **STM32CubeProgrammer**, select **ST-LINK** connection, click **Connect**
2. Click **Open file**, select `build/l4s5_anbo_0.1.0.hex`
3. Click **Download** — the start address is auto-detected from the HEX file
4. Click **Disconnect**, press the board RESET button

### CLI

```bash
# Using STM32CubeProgrammer CLI
STM32_Programmer_CLI -c port=SWD -w build/l4s5_anbo_0.1.0.bin 0x08000000 -v -rst

# Or using OpenOCD
openocd -f interface/stlink.cfg -f target/stm32l4x.cfg \
    -c "program build/l4s5_anbo_0.1.0.bin 0x08000000 verify reset exit"
```

Serial output is available on the ST-LINK Virtual COM Port (USART1, 115200-8N1).

## Unit Testing

Kernel modules are tested on the **host PC** (x86/x64) using [Ceedling](https://github.com/ThrowTheSwitch/Ceedling) (Unity + CMock). Hardware-dependent code is auto-mocked; a thin arch shim (`test/support/anbo_arch_host.c`) provides tick/IRQ stubs.

### Running Tests

Test scripts auto-download Ruby and Ceedling on first run — no manual setup needed.

| Action | CMD | PowerShell | Bash |
|--------|-----|------------|------|
| Run all tests | `test.bat` | `.\test.ps1` | `./test.sh` |
| Single module | `test.bat test:anbo_rb` | `.\test.ps1 test:anbo_rb` | `./test.sh test:anbo_rb` |
| Clean artifacts | `test.bat clean` | `.\test.ps1 clean` | `./test.sh clean` |

### Test Modules

| File | Covers |
|------|--------|
| `test/test_anbo_rb.c` | Ring buffer (push/pop, wrap, overflow) |
| `test/test_anbo_timer.c` | Software timer (one-shot, periodic, cancel) |
| `test/test_anbo_callback.c` | EBus subscribe/publish callback dispatch |
| `test/test_anbo_isr.c` | ISR-safe pool alloc + event queue post |

### Configuration

Test settings are in `project.yml` (Ceedling config). Key overrides for host testing:

- `ANBO_CONF_WDT=0`, `ANBO_CONF_IDLE_SLEEP=0` — disable hardware-only features
- `ANBO_CONF_TRACE=0` — disable instrumentation hooks
- Pool / EBus / Timer counts reduced to minimal sizes for fast host execution

## Project Structure

```
Anbo_L4s5_App/
├── CMakeLists.txt                  # Top-level firmware target
├── build.bat / build.ps1 / build.sh  # One-click build scripts
├── cmake/
│   └── arm-none-eabi.cmake         # Toolchain file (auto-downloads ARM GCC)
├── app/
│   ├── main.c                      # app_init() + app_run() super-loop
│   ├── app_config.h / .c           # NVM-backed persistent configuration
│   ├── app_signals.h               # Business signal definitions
│   ├── app_fault.h                 # Fault ID / severity / state definitions
│   ├── app_fault_mgr.h / .c        # FaultManager (register/report/clear/recover)
│   ├── app_sensor.h / .c           # ADC temperature sensor (Pool producer + fault reporting)
│   ├── app_imu.h / .c              # IMU vibration detection + deep sleep arm (LSM6DSL)
│   ├── app_controller.h / .c       # 3-state FSM (Normal/Alarm/Fault) + WDT slot
│   ├── app_ui.h / .c               # LED blink + UART temp display
│   └── app_sleep.h / .c            # Deep sleep (decomposed: prepare/stop2/check/maintenance/resume)
├── anbo/                               # ← Git submodule (Anbo Kernel)
│   ├── CMakeLists.txt              # Kernel static library
│   ├── kernel/
│   │   ├── include/                # Public headers (anbo_*.h)
│   │   └── src/                    # Kernel modules
│   └── port/
│       └── stm32l4s5/
│           ├── CMakeLists.txt      # Port library + SDK auto-download
│           ├── b_l4s5i_hw.*        # Board hardware init
│           ├── b_l4s5i_port.c      # Arch HAL implementation
│           ├── b_l4s5i_uart_drv.*  # ISR-driven USART1 driver
│           ├── b_l4s5i_i2c_drv.*   # I2C2 master driver (LSM6DSL communication)
│           ├── b_l4s5i_imu_drv.*   # LSM6DSL 6-axis IMU driver (FIFO + wake-up config)
│           ├── b_l4s5i_flash_drv.* # Internal Flash NVM driver
│           ├── b_l4s5i_ospi_drv.*  # OCTOSPI1 bus + MX25R6435F chip driver
│           ├── b_l4s5i_ospi_flash_drv.* # External NVM logic (chip-agnostic)
│           ├── startup_stm32l4s5xx.s
│           └── STM32L4S5VITx_FLASH.ld
├── tools/                          # Auto-downloaded (git-ignored)
└── .gitignore
```

## Memory Layout

| Region | Address | Size | Usage |
|--------|---------|------|-------|
| FLASH | 0x08000000 | 2 MB | .text, .rodata |
| SRAM1 | 0x20000000 | 192 KB | .data, .bss, stack (32 KB) |
| SRAM2 | 0x10000000 | 64 KB | Kernel pools (ECC, Standby-retained) |
| SRAM3 | 0x20040000 | 384 KB | Reserved |

## Demo Application

The firmware includes a complete **temperature alarm demo** that exercises
all kernel features:

### Data Flow

```
ADC1 ch17 (internal temp)
  │  1-second periodic timer
  │
  ▼
Sensor module
  Pool_Alloc(TempEvent) → EvtQ_Post
  │
  ▼  (main-loop drain)
Pool_Dispatch → EBus broadcast APP_SIG_TEMP_UPDATE
  ├───────────────────────────────────────────────────────────┬──┤
  │  Controller FSM (Normal/Alarm/Fault)                          │  │
  │    temp ≥ threshold? → Alarm → PublishSig(ALARM_STATE, 1)   │  │
  │    ADC out of range? → Fault_Report → FAULT_SET             │  │
  │      → enter Fault state (remember pre_fault_state)          │  │
  │      → auto-recover via 500ms scan → FAULT_CLR              │  │
  │                              │                                │  │
  │                              ▼                                │  │
  │                         UI alarm_handler                     │  │
  │                         LED2 fast blink (100 ms)             │  │
  │                                                              │  │
  └───────────────────────────────────────────────────────────┴──┘
                                              UI temp_display_handler
                                              "temp = 31.2 C" on UART
```

### EBus Subscription Map

| Signal | Subscriber 1 | Subscriber 2 |
|--------|-------------|-------------|
| `ANBO_SIG_USER_BUTTON` (0x0020) | Button handler (arm blink timer) | Sleep module (long-press 3 s) |
| `ANBO_SIG_UART_RX` (0x0010) | UART echo handler | — |
| `APP_SIG_IMU_INT1` (0x0030) | IMU FIFO readout handler | — |
| `APP_SIG_TEMP_UPDATE` (0x0100) | Controller FSM (threshold check) | UI (UART temp display) |
| `APP_SIG_IMU_UPDATE` (0x0110) | Vibration log output | — |
| `APP_SIG_THRESHOLD_SET` (0x0101) | Controller FSM (update threshold) | — |
| `APP_SIG_ALARM_STATE` (0x0102) | UI (LED blink rate) | — |
| `APP_SIG_FAULT_SET` (0x0200) | Controller FSM (enter Fault state) | — |
| `APP_SIG_FAULT_CLR` (0x0201) | Controller FSM (recover to pre-fault state) | — |
| `APP_SIG_FAULT_LATCHED` (0x0202) | Controller FSM (stay in Fault) | — |

### Build Size (all features enabled)

| Region | Used | Available | Usage |
|--------|------|-----------|-------|
| FLASH | ~25 KB | 2 MB | 1.2% |
| SRAM1 | ~9.2 KB | 192 KB | 4.7% |
| SRAM2 | 768 B | 64 KB | 1.2% |

## Configuration

### Kernel Config (`anbo_config.h`)

| Macro | Default | Description |
|-------|---------|-------------|
| `ANBO_CONF_IDLE_SLEEP` | 1 | Stop 2 low-power idle |
| `ANBO_CONF_WDT` | 1 | Software watchdog |
| `ANBO_CONF_POOL` | 1 | Block pool + async event queue |
| `ANBO_CONF_TRACE` | 1 | Event/FSM instrumentation hooks |
| `ANBO_CONF_POOL_BLOCK_SIZE` | 64 | Pool block size (bytes) |
| `ANBO_CONF_POOL_BLOCK_COUNT` | 20 | Number of pool blocks |
| `ANBO_CONF_EVTQ_DEPTH` | 32 | Async event queue depth |

### App Config (`app_config.h`)

| Macro | Default | Description |
|-------|---------|-------------|
| `APP_CONF_PARAM_FLASH` | 1 | Persistent Flash-backed config |
| `APP_CONF_PARAM_FLASH_USE_EXT` | 0 | 0 = internal Flash, 1 = external OSPI |
| `APP_CONF_PARAM_FLASH_INT_ADDR` | 0x081FF000 | Internal NVM page address |
| `APP_CONF_PARAM_FLASH_INT_PAGES` | 1 | Number of pages for wear-levelling rotation |
| `APP_CONF_PARAM_FLASH_EXT_ADDR` | 0x7FF000 | External NVM sector address |
| `APP_CONF_PARAM_FLASH_EXT_PAGES` | 1 | Number of sectors for wear-levelling rotation |
| `APP_CONF_LOG_FLASH` | 1 | Persistent Flash-backed log |
| `APP_CONF_LOG_FLASH_USE_EXT` | 1 | 0 = internal Flash, 1 = external OSPI |
| `APP_CONF_SLEEP_FREEZE_IWDG` | 0 | 0 = periodic IWDG feed (safe), 1 = freeze IWDG in Stop 2 via Option Byte (auto bidirectional OB switch on boot) |
| `APP_CONF_SLEEP_MAINTENANCE_LOG` | 1 | 1 = print + flush log on each LPTIM wake leg, 0 = silent |

## Hardware Pin Mapping

| Function | Pin | Config |
|----------|-----|--------|
| LED2 | PB14 | Push-pull, active high |
| User Button | PC13 | EXTI falling edge, pull-up |
| USART1 TX (VCP) | PB6 | AF7 |
| USART1 RX (VCP) | PB7 | AF7 |
| I2C2 SCL | PB10 | AF4, open-drain |
| I2C2 SDA | PB11 | AF4, open-drain |
| IMU INT1 (LSM6DSL) | PD11 | EXTI rising edge (wake-up from Stop 2) |

## License

See [LICENSE](LICENSE) for details.
