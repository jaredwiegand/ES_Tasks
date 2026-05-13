#!/usr/bin/env python3
"""
mock_server.py  –  mathdev server with software-simulated kernel device
========================================================================
Identical to server.py but replaces KernelInterface with a pure-Python
implementation.  Useful for development/testing on machines without
the kernel module loaded (e.g. CI, macOS, Windows WSL without kbuild).

Usage:
    python3 mock_server.py [--socket PATH] [--log-level LEVEL]
"""

from __future__ import annotations

import argparse
import errno as _errno
import logging
import os
import signal
import socket
import sys
import threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "proto"))
import protocol as proto

logging.basicConfig(
    format="[%(asctime)s] %(levelname)-8s %(name)s: %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("mathdev.mock_server")

OP_NAME_TO_CODE = {op.name: op.value for op in proto.OpCode}


class MockKernelInterface:
    """Pure-Python drop-in replacement for KernelInterface."""

    def open(self):
        log.info("Mock kernel device opened (no real /dev/mathdev).")

    def close(self):
        pass

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, *_):
        self.close()

    def query_ops(self):
        return proto.AVAILABLE_OPS

    def calculate(self, op_name: str, a: int, b: int) -> int:
        match op_name:
            case "ADD":
                log.info("Mock kernel: Calculating %d + %d!", a, b)
                return a + b
            case "SUB":
                log.info("Mock kernel: Calculating %d - %d!", a, b)
                return a - b
            case "MUL":
                log.info("Mock kernel: Calculating %d * %d!", a, b)
                return a * b
            case "DIV":
                if b == 0:
                    log.warning("Mock kernel: Division by zero!")
                    raise OSError(_errno.EDOM, "Math argument out of domain")
                log.info("Mock kernel: Calculating %d / %d!", a, b)
                return a // b
            case _:
                raise ValueError(f"Unknown operator: {op_name}")


# Import the rest of server machinery
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "server"))

# Patch KernelInterface before import
import server as _server  # noqa: E402
_server.KernelInterface = MockKernelInterface  # type: ignore[attr-defined]
from server import ClientHandler, MathServer  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description="mathdev mock server (no kernel module needed)")
    parser.add_argument("--socket",    default=proto.DEFAULT_SOCKET)
    parser.add_argument("--log-level", default="INFO",
                        choices=["DEBUG", "INFO", "WARNING", "ERROR"])
    args = parser.parse_args()

    logging.getLogger().setLevel(args.log_level)
    log.setLevel(args.log_level)

    server = MathServer(args.socket, "/dev/null")  # device_path unused

    def patched_run():
        # Clean up stale socket
        try:
            os.unlink(server.socket_path)
        except FileNotFoundError:
            pass

        kernel = MockKernelInterface()
        kernel.open()

        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as srv:
            srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            srv.bind(server.socket_path)
            os.chmod(server.socket_path, 0o666)
            srv.listen(16)
            log.info("mathdev MOCK server listening on %s", server.socket_path)

            def _shutdown(signum, frame):
                log.info("Signal %d received, shutting down.", signum)
                srv.close()

            signal.signal(signal.SIGINT,  _shutdown)
            signal.signal(signal.SIGTERM, _shutdown)

            while True:
                try:
                    conn, addr = srv.accept()
                except OSError:
                    break
                handler = ClientHandler(conn, addr, kernel)
                handler.start()

        try:
            os.unlink(server.socket_path)
        except FileNotFoundError:
            pass
        kernel.close()
        log.info("Mock server stopped.")

    patched_run()


if __name__ == "__main__":
    main()
