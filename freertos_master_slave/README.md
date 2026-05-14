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

## Simulation Results

See [SIMULATION_RESULTS.md](SIMULATION_RESULTS.md) for a fully annotated
30-second run demonstrating state synchronisation, fault detection, transient
fault debouncing, and recovery across four independent fault cycles.

## Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| Queues-only IPC | No shared memory means no mutex contention or priority inversion risk; producer and consumer are fully decoupled in time |
| Pure SM functions separated from task code | `device_a_process_event()` and `device_b_next_state()` have zero FreeRTOS dependency and compile under `BUILD_TESTS=ON` for direct unit testing |
| Dedicated logger task | All `printf` output serialised through one thread; device tasks pay only a non-blocking queue-send cost and are never stalled by I/O |
| Fixed `RAND_SEED` | Every run is deterministic and reproducible; change the seed to explore different transition sequences |
| Non-blocking queue sends | Device tasks never stall waiting for a queue slot; a dropped log message is preferable to blocking a state transition |

For full rationale on each choice see the *Key Design Decisions* section of `DESIGN.docx`.

## Potential Improvements and Extensions

- **Additional devices** — the queue-based architecture scales naturally; adding a Device C requires only a new task and queue pair with no changes to existing code
- **Persistent logging** — redirect the logger task's output to a file or ring buffer for post-run analysis without modifying the device tasks
- **Runtime fault injection** — add a signal handler or CLI command to force Device B into FAULT on demand, enabling targeted testing without waiting on the RNG
- **Software watchdog** — add a watchdog task that detects if either device task stops producing state changes within a configurable timeout
