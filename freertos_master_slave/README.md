# freertos_master_slave

A FreeRTOS simulation demonstrating master/slave state machines communicating
exclusively via IPC queues.

## Overview

Two devices run as FreeRTOS tasks under the POSIX (Linux) or Win32 (Windows)
simulator port:

| Device | Role | States |
|--------|------|--------|
| Device A (master) | Monitors Device B; issues reset on persistent fault | IDLE → PROCESSING → ERROR |
| Device B (slave) | Transitions autonomously; responds to reset command | SLEEP ↔ ACTIVE ↔ FAULT |

A third task serialises all log output to stdout through a dedicated log queue.
No shared memory with concurrent access is used — all state information travels
through FreeRTOS queues.

## Project Layout

```
freertos_master_slave/
├── CMakeLists.txt        Build system; fetches FreeRTOS-Kernel and Unity
├── FreeRTOSConfig.h      Kernel configuration (tick rate, heap, priorities)
├── README.md             This file
├── REQUIREMENTS.docx     Requirements specification
├── DESIGN.docx           Software design document
├── scripts/
│   └── run_tests.sh      Build and run the full test suite
├── src/
│   ├── main.c            Entry point: queues, tasks, scheduler
│   ├── device_a.c/.h     Master state machine and FreeRTOS task
│   ├── device_b.c/.h     Slave state machine and FreeRTOS task
│   ├── ipc.c/.h          Queue handles and message type definitions
│   ├── logger.c/.h       Logger task and logger_log() helper
│   └── sim_config.h      Compile-time tuning constants
└── tests/
    ├── test_device_a_sm.c   Unity tests: Device A transitions
    ├── test_device_b_sm.c   Unity tests: Device B transitions
    └── test_ipc_types.c     Unity tests: message type sizes and enum values
```

## Prerequisites

### Linux

```bash
sudo apt install build-essential cmake git
```

CMake 3.18 or later is required for `FetchContent_MakeAvailable`.

### Windows

MinGW-w64 (or MSVC) and CMake 3.18+.

## Building and Running

### Linux (POSIX port)

```bash
cmake -B build
cmake --build build
./build/freertos_sim
```

### Windows (Win32 port)

```bat
cmake -B build -G "MinGW Makefiles"
cmake --build build
build\freertos_sim.exe
```

FreeRTOS-Kernel v11.1.0 and (when `BUILD_TESTS=ON`) Unity v2.5.2 are fetched
automatically by CMake on first configure; an internet connection is required.

## Running Tests

### Using the test script (recommended)

```bash
bash scripts/run_tests.sh          # build + unit tests + sim smoke test
bash scripts/run_tests.sh --unit   # unit tests only
```

The script builds with `BUILD_TESTS=ON`, runs each Unity suite with per-test
PASS/FAIL output, and (in full mode) runs a 5-second simulation smoke test.

### Manually

```bash
cmake -B build -DBUILD_TESTS=ON
cmake --build build

# Run all suites (summary output)
cd build && ctest

# Run all suites with per-test PASS/FAIL lines
cd build && ctest -V

# Run a single suite
./build/test_device_a_sm
./build/test_device_b_sm
./build/test_ipc_types
```

## Configuration

All tunable parameters are compile-time constants in `src/sim_config.h`.
Override any of them at configure time:

```bash
cmake -B build -DCMAKE_C_FLAGS="-DSIM_DURATION_MS=60000 -DFAULT_THRESHOLD=5"
```

| Parameter | Default | Description |
|-----------|---------|-------------|
| `SIM_DURATION_MS` | 30000 | Total simulation run time (ms) |
| `DEVICE_A_POLL_MS` | 250 | Device A evaluation-cycle period (ms) |
| `DEVICE_B_TICK_MS` | 500 | Device B transition-check interval (ms) |
| `FAULT_THRESHOLD` | 3 | Consecutive FAULT observations before reset |
| `RESET_TIMEOUT_MS` | 2000 | Device A recovery wait before re-issuing reset |
| `RAND_SEED` | 42 | Fixed seed for reproducible Device B transitions |

## Sample Output

```
freertos_master_slave simulation
=================================
Duration : 30000 ms
Threshold: 3 consecutive FAULT observations

[TICK:0000000001] [DEVICE_B] task started  state=SLEEP
[TICK:0000000001] [DEVICE_A] task started  state=IDLE
[TICK:0000000501] [DEVICE_B] SLEEP -> ACTIVE
[TICK:0000000501] [DEVICE_A] IDLE -> PROCESSING
[TICK:0000003501] [DEVICE_B] ACTIVE -> SLEEP
[TICK:0000003501] [DEVICE_A] PROCESSING -> IDLE
[TICK:0000007001] [DEVICE_B] ACTIVE -> FAULT
[TICK:0000007001] [DEVICE_A] fault_count=1
[TICK:0000007251] [DEVICE_A] fault_count=2
[TICK:0000007501] [DEVICE_A] fault_count=3
[TICK:0000007501] [DEVICE_A] reset command issued
[TICK:0000007501] [DEVICE_A] PROCESSING -> ERROR
[TICK:0000008001] [DEVICE_B] FAULT -> SLEEP  (reset received)
[TICK:0000008001] [DEVICE_A] ERROR -> IDLE  (recovery complete)

Simulation complete.
```

## License

MIT
