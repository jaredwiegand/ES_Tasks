# mathdev — Linux Kernel Math Character Device

A full-stack Linux project that exposes basic math operations through a
kernel character device, an IPC server gateway, and multiple clients.

```
┌──────────────────┐       Unix Domain       ┌───────────────────┐       ioctl        ┌──────────────────┐
│  Python Client   │ ◄──── Socket (JSON) ───► │  Python Server    │ ◄──────────────── │ /dev/mathdev     │
│  (client.py)     │                          │  (server.py)      │                   │ (mathdev.ko)     │
└──────────────────┘                          └───────────────────┘                   └──────────────────┘
┌──────────────────┐               ▲
│  C Client        │ ──────────────┘
│  (mathdev-client)│
└──────────────────┘
```

## Features

| Component        | Description |
|-----------------|-------------|
| `kernel/mathdev.c`       | Kernel char device. Handles `ADD`, `SUB`, `MUL`, `DIV` via `ioctl` |
| `server/server.py`       | Multi-client Unix-socket server. Bridges JSON protocol ↔ ioctl |
| `server/mock_server.py`  | Drop-in mock server (no kernel module required, good for CI/dev) |
| `client_python/client.py`| Python 3.10+ interactive terminal client |
| `client_c/client.c`      | C11 terminal client (bonus), identical UX to Python client |
| `proto/protocol.py`      | Shared protocol constants, message builders, framing helpers |

---

## Requirements

### Kernel module
- Linux kernel **≥ 5.x** (tested on 5.15, 6.1, 6.6)
- Kernel headers for your running kernel: `linux-headers-$(uname -r)`
- `gcc`, `make`

### Server & Python client
- Python **≥ 3.10** (uses structural pattern matching)
- No third-party Python packages required

### C client
- `gcc` ≥ 7 or `clang` ≥ 6
- `cmake` ≥ 3.18
- Internet access **or** `libcjson-dev` installed (CMake will auto-fetch cJSON otherwise)

---

## Quick Start

### 1 — Build everything

```bash
# Build the C client (and optionally the kernel module)
cmake -B build -DBUILD_C_CLIENT=ON
cmake --build build

# Build just the kernel module separately
make -C kernel
```

### 2 — Load the kernel module

```bash
sudo scripts/load_module.sh
# Verify:
ls -la /dev/mathdev
dmesg | tail -5
```

### 3 — Start the server

```bash
# Real server (requires /dev/mathdev)
scripts/start_server.sh

# OR mock server (no kernel module needed — great for testing)
python3 server/mock_server.py
```

### 4 — Run clients (in separate terminals)

```bash
# Python client
python3 client_python/client.py

# C client (multiple simultaneous clients supported)
./build/mathdev-client

# Custom socket path
python3 client_python/client.py --socket /tmp/mathdev.sock
```

---

## Sample Session

```
╔══════════════════════════════════════╗
║       mathdev — kernel math ops      ║
╚══════════════════════════════════════╝
  Connected as python-a3f2c1b0

  1) Add two signed integers
  2) Subtract two signed integers
  3) Multiply two signed integers
  4) Divide two signed integers
  5) Exit

Enter command: 1
Enter operand 1: 42
Enter operand 2: 37
Sending request...
Request OKAY...
Receiving response...
Result is 79!
```

Server output:
```
[10:23:01] INFO     mathdev.server: Client connected! [client-1]
[10:23:04] INFO     mathdev.server: [client-1] Received request ADD(42,37).
[10:23:04] INFO     mathdev.server: [client-1] Sending result 79.
```

Kernel log (`dmesg`):
```
mathdev: Device opened! (pid=1234)
mathdev: Calculating 42 + 37!
```

---

## Protocol Specification

All messages are **length-prefixed JSON** over a Unix-domain stream socket.

```
[ 4 bytes big-endian uint32 length ][ UTF-8 JSON payload ]
```

### Handshake

```
Client → Server:   {"type":"HELLO","version":"1.0","client_id":"<id>"}
Server → Client:   {"type":"HELLO_ACK","version":"1.0","server_id":"mathdev-server",
                    "ops":[{"op_code":1,"name":"ADD","description":"..."},...]}
```

### Calculation

```
Client → Server:   {"type":"CALC","req_id":1,"op":"ADD","a":42,"b":37}
Server → Client:   {"type":"CALC_ACK","req_id":1,"op":"ADD","a":42,"b":37,"result":79}
```

### Error response

```
Server → Client:   {"type":"ERROR","req_id":1,"error_code":"ERR_DIV_ZERO",
                    "message":"Division by zero is undefined"}
```

### Disconnect

```
Client → Server:   {"type":"BYE","client_id":"<id>"}
Server → Client:   {"type":"BYE_ACK","message":"Goodbye!"}
```

### Error codes

| Code              | Meaning |
|-------------------|---------|
| `ERR_UNKNOWN_OP`  | Operator not recognised |
| `ERR_DIV_ZERO`    | Division by zero |
| `ERR_BAD_REQUEST` | Malformed JSON or missing fields |
| `ERR_DEVICE`      | Kernel ioctl returned an error |
| `ERR_INTERNAL`    | Unexpected server-side error |

---

## Kernel ioctl Interface

The kernel module exposes two ioctl commands (magic byte `'m'`):

| ioctl                 | Direction | Description |
|-----------------------|-----------|-------------|
| `MATH_IOCTL_CALC`     | RW        | Perform calculation; in/out: `struct math_request` |
| `MATH_IOCTL_QUERY_OPS`| R         | List available ops; out: `struct math_ops_info` |

```c
struct math_request {
    int64_t  a;       // first operand
    int64_t  b;       // second operand
    int64_t  result;  // written by kernel
    uint32_t op;      // MATH_OP_ADD/SUB/MUL/DIV
    uint32_t _pad;
};
```

Errors returned by `ioctl()`:

| errno   | Meaning |
|---------|---------|
| `EDOM`  | Division by zero |
| `EINVAL`| Unknown operator |
| `EFAULT`| Bad user pointer |
| `ENOTTY`| Unknown ioctl command |

---

## Project Layout

```
mathdev/
├── CMakeLists.txt          ← Top-level CMake (C client + kernel target)
├── README.md
├── kernel/
│   ├── mathdev.c           ← Kernel module source
│   ├── mathdev.h           ← Shared header (kernel + userspace)
│   └── Makefile            ← kbuild Makefile
├── proto/
│   └── protocol.py         ← Protocol constants, framing, message builders
├── server/
│   ├── server.py           ← Production server (uses /dev/mathdev)
│   └── mock_server.py      ← Software-only server (no kernel module)
├── client_python/
│   └── client.py           ← Python 3.10+ interactive client
├── client_c/
│   ├── client.c            ← C11 interactive client (bonus)
│   └── protocol.h          ← C protocol constants
└── scripts/
    ├── load_module.sh      ← Build + insmod + set permissions
    ├── unload_module.sh    ← rmmod
    └── start_server.sh     ← Start production server
```

---

## Development Notes

### Running without a kernel module
Use `mock_server.py` — it simulates the kernel device entirely in Python.
Useful for development on macOS, inside Docker, or in CI pipelines.

### Multiple simultaneous clients
The server spawns a thread per client.  The kernel ioctl is protected by a
`mutex` (server side) and per-instance state is held per `open()` call.

### Kernel version compatibility
The module handles the `class_create()` API change in kernel 6.4 via a
compile-time `LINUX_VERSION_CODE` guard.

### Extending with new operations
1. Add `MATH_OP_<NAME>` constant to `kernel/mathdev.h`
2. Handle the new case in `mathdev_calculate()` in `kernel/mathdev.c`
3. Add to the `math_ops_info` table in `mathdev_ioctl()`
4. Add to `proto/protocol.py` `AVAILABLE_OPS` and `OpCode` enum
5. No client changes needed — the menu is built from the server announcement

---

## License

GPL-2.0 (kernel module) / MIT (userspace components)
