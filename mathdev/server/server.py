#!/usr/bin/env python3
"""
server.py  –  mathdev Unix-domain-socket server
================================================

Acts as a gateway between multiple concurrent Python/C clients and the
/dev/mathdev kernel character device.

Usage:
    python3 server.py [--socket PATH] [--device PATH] [--log-level LEVEL]

The server:
  • Listens on a Unix-domain stream socket (default: /tmp/mathdev.sock)
  • Handles each client in its own thread
  • Performs ioctl calls on /dev/mathdev for every CALC request
  • Implements the length-prefixed JSON protocol defined in protocol.py
  • Returns service announcement (HELLO_ACK) with live operator list from kernel
"""

from __future__ import annotations

import argparse
import ctypes
import errno
import fcntl
import logging
import os
import signal
import socket
import struct
import sys
import threading
import time
from pathlib import Path
from typing import Any

# Allow running from project root without installing
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "proto"))
import protocol as proto

# ── Logging ───────────────────────────────────────────────────────────────────
logging.basicConfig(
    format="[%(asctime)s] %(levelname)-8s %(name)s: %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("mathdev.server")

# ── ioctl / kernel interface ──────────────────────────────────────────────────
# These must match kernel/mathdev.h exactly.

MATHDEV_IOC_MAGIC = ord('m')

# struct math_request  { s64 a; s64 b; s64 result; u32 op; u32 _pad; }
class MathRequest(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("a",      ctypes.c_int64),
        ("b",      ctypes.c_int64),
        ("result", ctypes.c_int64),
        ("op",     ctypes.c_uint32),
        ("_pad",   ctypes.c_uint32),
    ]

# struct math_op_desc  { u32 op_code; char name[8]; char description[48]; }
class MathOpDesc(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("op_code",     ctypes.c_uint32),
        ("name",        ctypes.c_char * 8),
        ("description", ctypes.c_char * 48),
    ]

# struct math_ops_info { u32 num_ops; u32 _pad; struct math_op_desc ops[4]; }
class MathOpsInfo(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("num_ops", ctypes.c_uint32),
        ("_pad",    ctypes.c_uint32),
        ("ops",     MathOpDesc * 4),
    ]

# _IOWR / _IOR macro equivalents
def _IOC(direction, type_, nr, size):
    IOC_NRBITS   = 8
    IOC_TYPEBITS = 8
    IOC_SIZEBITS = 14
    IOC_NRSHIFT   = 0
    IOC_TYPESHIFT = IOC_NRSHIFT   + IOC_NRBITS
    IOC_SIZESHIFT = IOC_TYPESHIFT + IOC_TYPEBITS
    IOC_DIRSHIFT  = IOC_SIZESHIFT + IOC_SIZEBITS
    return (direction << IOC_DIRSHIFT) | (type_ << IOC_TYPESHIFT) | \
           (nr << IOC_NRSHIFT) | (size << IOC_SIZESHIFT)

_IOC_WRITE = 1
_IOC_READ  = 2

MATH_IOCTL_CALC      = _IOC(_IOC_READ | _IOC_WRITE, MATHDEV_IOC_MAGIC, 1, ctypes.sizeof(MathRequest))
MATH_IOCTL_QUERY_OPS = _IOC(_IOC_READ,               MATHDEV_IOC_MAGIC, 2, ctypes.sizeof(MathOpsInfo))

OP_NAME_TO_CODE = {op.name: op.value for op in proto.OpCode}

class KernelInterface:
    """Thread-safe wrapper around /dev/mathdev ioctl calls."""

    def __init__(self, device_path: str = "/dev/mathdev"):
        self.device_path = device_path
        self._lock = threading.Lock()
        self._fd: int | None = None

    def open(self) -> None:
        self._fd = os.open(self.device_path, os.O_RDWR)
        log.info("Opened kernel device %s (fd=%d)", self.device_path, self._fd)

    def close(self) -> None:
        if self._fd is not None:
            os.close(self._fd)
            self._fd = None

    def query_ops(self) -> list[dict[str, Any]]:
        """Return the list of operations from the kernel module."""
        info = MathOpsInfo()
        with self._lock:
            fcntl.ioctl(self._fd, MATH_IOCTL_QUERY_OPS, info)
        ops = []
        for i in range(info.num_ops):
            d = info.ops[i]
            ops.append({
                "op_code":     d.op_code,
                "name":        d.name.decode("utf-8"),
                "description": d.description.decode("utf-8"),
            })
        return ops

    def calculate(self, op_name: str, a: int, b: int) -> int:
        """
        Perform a calculation via ioctl.
        Returns the integer result.
        Raises OSError with appropriate errno on failure.
        """
        op_code = OP_NAME_TO_CODE.get(op_name)
        if op_code is None:
            raise ValueError(f"Unknown operator: {op_name}")

        req = MathRequest(a=a, b=b, op=op_code)
        with self._lock:
            fcntl.ioctl(self._fd, MATH_IOCTL_CALC, req)
        return req.result

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, *_):
        self.close()


# ── Client handler ────────────────────────────────────────────────────────────

class ClientHandler(threading.Thread):
    """One thread per connected client."""

    _id_lock   = threading.Lock()
    _id_counter = 0

    def __init__(self, conn: socket.socket, addr: Any, kernel: KernelInterface):
        super().__init__(daemon=True)
        with ClientHandler._id_lock:
            ClientHandler._id_counter += 1
            self.client_num = ClientHandler._id_counter
        self.conn   = conn
        self.addr   = addr
        self.kernel = kernel
        self.name   = f"client-{self.client_num}"
        self._req_count = 0

    # -- main loop -----------------------------------------------------------

    def run(self):
        log.info("Client connected! [%s]", self.name)
        try:
            self._loop()
        except ConnectionError as exc:
            log.info("[%s] Connection closed: %s", self.name, exc)
        except Exception as exc:  # noqa: BLE001
            log.exception("[%s] Unexpected error: %s", self.name, exc)
        finally:
            self.conn.close()
            log.info("[%s] Handler exiting.", self.name)

    def _loop(self):
        handshaken = False
        while True:
            msg = proto.recv_msg(self.conn)
            if msg is None:
                log.info("[%s] Peer disconnected.", self.name)
                break

            mtype = msg.get("type")

            if mtype == proto.MSG_HELLO:
                self._handle_hello(msg)
                handshaken = True

            elif mtype == proto.MSG_CALC:
                if not handshaken:
                    self._send_error(None, proto.ERR_BAD_REQUEST,
                                     "Must send HELLO before CALC")
                else:
                    self._handle_calc(msg)

            elif mtype == proto.MSG_BYE:
                self._handle_bye(msg)
                break   # clean disconnect

            else:
                self._send_error(msg.get("req_id"), proto.ERR_BAD_REQUEST,
                                 f"Unknown message type: {mtype!r}")

    # -- message handlers ----------------------------------------------------

    def _handle_hello(self, msg: dict):
        client_id = msg.get("client_id", "unknown")
        log.info("[%s] HELLO from client_id=%s", self.name, client_id)
        try:
            ops = self.kernel.query_ops()
        except Exception:  # noqa: BLE001
            log.warning("[%s] kernel query_ops failed, using static list", self.name)
            ops = proto.AVAILABLE_OPS
        ack = proto.build_hello_ack(ops)
        proto.send_msg(self.conn, ack)

    def _handle_calc(self, msg: dict):
        req_id = msg.get("req_id")
        op     = msg.get("op")
        a_raw  = msg.get("a")
        b_raw  = msg.get("b")

        # Validate fields
        if op is None or a_raw is None or b_raw is None:
            self._send_error(req_id, proto.ERR_BAD_REQUEST,
                             "CALC must include 'op', 'a', 'b'")
            return

        if op not in OP_NAME_TO_CODE:
            self._send_error(req_id, proto.ERR_UNKNOWN_OP,
                             f"Unknown operator: {op!r}")
            return

        try:
            a = int(a_raw)
            b = int(b_raw)
        except (TypeError, ValueError):
            self._send_error(req_id, proto.ERR_BAD_REQUEST,
                             "Operands must be integers")
            return

        log.info("[%s] Received request %s(%d,%d).", self.name, op, a, b)

        try:
            result = self.kernel.calculate(op, a, b)
        except OSError as exc:
            if exc.errno == errno.EDOM:
                log.warning("[%s] Division by zero.", self.name)
                self._send_error(req_id, proto.ERR_DIV_ZERO,
                                 "Division by zero is undefined")
            else:
                log.error("[%s] Kernel device error: %s", self.name, exc)
                self._send_error(req_id, proto.ERR_DEVICE,
                                 f"Kernel device error: {exc}")
            return
        except ValueError as exc:
            self._send_error(req_id, proto.ERR_UNKNOWN_OP, str(exc))
            return
        except Exception as exc:  # noqa: BLE001
            log.exception("[%s] Internal error during calc", self.name)
            self._send_error(req_id, proto.ERR_INTERNAL, str(exc))
            return

        self._req_count += 1
        log.info("[%s] Sending result %d.", self.name, result)
        proto.send_msg(self.conn, proto.build_calc_ack(req_id, op, a, b, result))

    def _handle_bye(self, msg: dict):
        log.info("[%s] BYE received (served %d requests).", self.name, self._req_count)
        proto.send_msg(self.conn, proto.build_bye_ack())

    def _send_error(self, req_id, code: str, message: str):
        log.warning("[%s] Sending error %s: %s", self.name, code, message)
        proto.send_msg(self.conn, proto.build_error(req_id, code, message))


# ── Server main ───────────────────────────────────────────────────────────────

class MathServer:
    def __init__(self, socket_path: str, device_path: str):
        self.socket_path = socket_path
        self.device_path = device_path
        self._running    = False

    def run(self):
        # Clean up stale socket
        try:
            os.unlink(self.socket_path)
        except FileNotFoundError:
            pass

        with KernelInterface(self.device_path) as kernel:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as srv:
                srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                srv.bind(self.socket_path)
                os.chmod(self.socket_path, 0o666)
                srv.listen(16)
                self._running = True
                log.info("mathdev server listening on %s", self.socket_path)

                def _shutdown(signum, frame):
                    log.info("Received signal %d, shutting down.", signum)
                    self._running = False
                    srv.close()

                signal.signal(signal.SIGINT,  _shutdown)
                signal.signal(signal.SIGTERM, _shutdown)

                while self._running:
                    try:
                        conn, addr = srv.accept()
                    except OSError:
                        break
                    handler = ClientHandler(conn, addr, kernel)
                    handler.start()

        try:
            os.unlink(self.socket_path)
        except FileNotFoundError:
            pass
        log.info("Server stopped.")


def main():
    parser = argparse.ArgumentParser(description="mathdev server")
    parser.add_argument("--socket",    default=proto.DEFAULT_SOCKET,
                        help="Unix socket path (default: %(default)s)")
    parser.add_argument("--device",    default="/dev/mathdev",
                        help="Kernel device path (default: %(default)s)")
    parser.add_argument("--log-level", default="INFO",
                        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
                        help="Logging level (default: %(default)s)")
    args = parser.parse_args()

    logging.getLogger().setLevel(args.log_level)
    log.setLevel(args.log_level)

    server = MathServer(args.socket, args.device)
    server.run()


if __name__ == "__main__":
    main()
