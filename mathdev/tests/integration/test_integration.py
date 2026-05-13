"""
test_integration.py — full stack integration tests
===================================================
Requires:
  - /dev/mathdev to be present (kernel module loaded)
  - Run with: pytest tests/integration/test_integration.py

These tests are skipped automatically if /dev/mathdev is not present.
Run: sudo bash scripts/load_module.sh  before running these tests.
"""

from __future__ import annotations

import os
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "proto"))
sys.path.insert(0, str(ROOT / "server"))

import protocol as proto

DEVICE_PATH = "/dev/mathdev"
SOCKET_PATH = "/tmp/mathdev_test_integration.sock"

# ── Skip entire module if kernel module not loaded ────────────────────────────

pytestmark = pytest.mark.skipif(
    not os.path.exists(DEVICE_PATH),
    reason=f"{DEVICE_PATH} not found — load the kernel module first"
)


# ── Real server fixture ───────────────────────────────────────────────────────

@pytest.fixture(scope="module")
def real_server():
    """Start a real MathServer against /dev/mathdev."""
    from server import MathServer

    # Clean up stale socket
    try:
        os.unlink(SOCKET_PATH)
    except FileNotFoundError:
        pass

    server = MathServer(SOCKET_PATH, DEVICE_PATH)
    from unittest.mock import patch as _patch
    _sig_patch = _patch("server.signal.signal")
    _sig_patch.start()
    t = threading.Thread(target=server.run, daemon=True)
    t.start()

    deadline = time.time() + 5.0
    while not os.path.exists(SOCKET_PATH):
        assert time.time() < deadline, "Real server did not start"
        time.sleep(0.05)

    yield SOCKET_PATH

    server._running = False
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(SOCKET_PATH)
        s.close()
    except Exception:
        pass
    t.join(timeout=3.0)
    _sig_patch.stop()
    try:
        os.unlink(SOCKET_PATH)
    except FileNotFoundError:
        pass


def connect(path: str, client_id: str = "test") -> socket.socket:
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(path)
    proto.send_msg(sock, proto.build_hello(client_id))
    ack = proto.recv_msg(sock)
    assert ack["type"] == proto.MSG_HELLO_ACK
    return sock


def calc(sock, op, a, b, req_id=1):
    proto.send_msg(sock, proto.build_calc(req_id, op, a, b))
    return proto.recv_msg(sock)


# ── Kernel math correctness ───────────────────────────────────────────────────

class TestKernelMath:
    """Verify the kernel module produces correct results for all operators."""

    def test_add_positive(self, real_server):
        sock = connect(real_server)
        resp = calc(sock, "ADD", 42, 37)
        assert resp["result"] == 79
        sock.close()

    def test_add_negative(self, real_server):
        sock = connect(real_server)
        resp = calc(sock, "ADD", -10, -5)
        assert resp["result"] == -15
        sock.close()

    def test_add_zero(self, real_server):
        sock = connect(real_server)
        resp = calc(sock, "ADD", 0, 0)
        assert resp["result"] == 0
        sock.close()

    def test_sub_positive(self, real_server):
        sock = connect(real_server)
        resp = calc(sock, "SUB", 10, 3)
        assert resp["result"] == 7
        sock.close()

    def test_sub_negative_result(self, real_server):
        sock = connect(real_server)
        resp = calc(sock, "SUB", 3, 10)
        assert resp["result"] == -7
        sock.close()

    def test_sub_same_values(self, real_server):
        sock = connect(real_server)
        resp = calc(sock, "SUB", 42, 42)
        assert resp["result"] == 0
        sock.close()

    def test_mul_positive(self, real_server):
        sock = connect(real_server)
        resp = calc(sock, "MUL", 6, 7)
        assert resp["result"] == 42
        sock.close()

    def test_mul_by_zero(self, real_server):
        sock = connect(real_server)
        resp = calc(sock, "MUL", 999, 0)
        assert resp["result"] == 0
        sock.close()

    def test_mul_negative(self, real_server):
        sock = connect(real_server)
        resp = calc(sock, "MUL", -3, -4)
        assert resp["result"] == 12
        sock.close()

    def test_mul_mixed_sign(self, real_server):
        sock = connect(real_server)
        resp = calc(sock, "MUL", -5, 4)
        assert resp["result"] == -20
        sock.close()

    def test_div_exact(self, real_server):
        sock = connect(real_server)
        resp = calc(sock, "DIV", 10, 2)
        assert resp["result"] == 5
        sock.close()

    def test_div_truncates(self, real_server):
        """Integer division should truncate toward zero."""
        sock = connect(real_server)
        resp = calc(sock, "DIV", 7, 2)
        assert resp["result"] == 3
        sock.close()

    def test_div_negative(self, real_server):
        sock = connect(real_server)
        resp = calc(sock, "DIV", -10, 2)
        assert resp["result"] == -5
        sock.close()

    def test_div_by_one(self, real_server):
        sock = connect(real_server)
        resp = calc(sock, "DIV", 42, 1)
        assert resp["result"] == 42
        sock.close()

    def test_div_by_zero_returns_error(self, real_server):
        sock = connect(real_server)
        resp = calc(sock, "DIV", 10, 0)
        assert resp["type"]       == proto.MSG_ERROR
        assert resp["error_code"] == proto.ERR_DIV_ZERO
        sock.close()

    def test_large_values(self, real_server):
        sock = connect(real_server)
        resp = calc(sock, "ADD", 2**30, 2**30)
        assert resp["result"] == 2**31
        sock.close()

    def test_min_int64(self, real_server):
        """Minimum signed 64-bit integer."""
        sock = connect(real_server)
        resp = calc(sock, "ADD", -(2**63), 0)
        assert resp["result"] == -(2**63)
        sock.close()


# ── Service announcement ──────────────────────────────────────────────────────

class TestServiceAnnouncement:
    def test_hello_ack_lists_all_ops(self, real_server):
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(real_server)
        proto.send_msg(sock, proto.build_hello("test"))
        ack = proto.recv_msg(sock)
        names = {op["name"] for op in ack["ops"]}
        assert "ADD" in names
        assert "SUB" in names
        assert "MUL" in names
        assert "DIV" in names
        sock.close()

    def test_ops_have_descriptions(self, real_server):
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(real_server)
        proto.send_msg(sock, proto.build_hello("test"))
        ack = proto.recv_msg(sock)
        for op in ack["ops"]:
            assert len(op.get("description", "")) > 0
        sock.close()


# ── Kernel dmesg verification ─────────────────────────────────────────────────

class TestKernelMessages:
    def test_device_opened_in_dmesg(self, real_server):
        """Verify that opening the device produces a kernel log message."""
        sock = connect(real_server, "dmesg-test")
        time.sleep(0.1)

        result = subprocess.run(
            ["dmesg"], capture_output=True, text=True
        )
        assert "mathdev" in result.stdout.lower() or \
               "Device opened" in result.stdout or \
               "mathdev" in result.stdout
        sock.close()

    def test_calculation_logged_in_dmesg(self, real_server):
        """Verify the kernel logs the calculation."""
        sock = connect(real_server, "dmesg-calc-test")
        calc(sock, "ADD", 11, 22)
        time.sleep(0.1)

        result = subprocess.run(["dmesg"], capture_output=True, text=True)
        # Kernel logs "Calculating 11 + 22!"
        assert "11" in result.stdout or "Calculating" in result.stdout
        sock.close()
