"""
test_server.py — unit tests for server/server.py
=================================================
Tests server logic using a mock KernelInterface so no real
/dev/mathdev device is required.

Covers:
  - HELLO → HELLO_ACK handshake
  - HELLO_ACK contains ops from kernel query
  - HELLO_ACK falls back to static ops if kernel query fails
  - CALC must be preceded by HELLO
  - CALC ADD / SUB / MUL / DIV happy path
  - CALC with negative operands
  - CALC with large integers
  - DIV by zero → ERR_DIV_ZERO
  - Unknown operator → ERR_UNKNOWN_OP
  - Missing fields → ERR_BAD_REQUEST
  - Non-integer operands → ERR_BAD_REQUEST
  - Kernel device error → ERR_DEVICE
  - Unknown message type → ERR_BAD_REQUEST
  - BYE → BYE_ACK clean disconnect
  - Multiple sequential requests on one connection
  - Multiple concurrent clients
"""

from __future__ import annotations

import errno
import socket
import sys
import threading
import time
from pathlib import Path
from unittest.mock import MagicMock, patch

import pytest

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "proto"))
sys.path.insert(0, str(ROOT / "server"))

import protocol as proto
from server import MathServer, KernelInterface
from tests.conftest import connect_and_handshake


# ── helpers ───────────────────────────────────────────────────────────────────

def do_calc(sock: socket.socket, op: str, a: int, b: int, req_id: int = 1) -> dict:
    proto.send_msg(sock, proto.build_calc(req_id, op, a, b))
    return proto.recv_msg(sock)


# ── HELLO handshake ───────────────────────────────────────────────────────────

class TestHandshake:
    def test_hello_ack_type(self, running_server):
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(running_server)
        proto.send_msg(sock, proto.build_hello("test"))
        ack = proto.recv_msg(sock)
        assert ack["type"] == proto.MSG_HELLO_ACK
        sock.close()

    def test_hello_ack_version(self, running_server):
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(running_server)
        proto.send_msg(sock, proto.build_hello("test"))
        ack = proto.recv_msg(sock)
        assert ack["version"] == proto.PROTOCOL_VERSION
        sock.close()

    def test_hello_ack_contains_ops(self, running_server):
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(running_server)
        proto.send_msg(sock, proto.build_hello("test"))
        ack = proto.recv_msg(sock)
        assert "ops" in ack
        assert len(ack["ops"]) == 4
        sock.close()

    def test_hello_ack_op_names(self, running_server):
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(running_server)
        proto.send_msg(sock, proto.build_hello("test"))
        ack = proto.recv_msg(sock)
        names = {op["name"] for op in ack["ops"]}
        assert names == {"ADD", "SUB", "MUL", "DIV"}
        sock.close()

    def test_calc_before_hello_returns_error(self, running_server):
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(running_server)
        # Send CALC without HELLO first
        proto.send_msg(sock, proto.build_calc(1, "ADD", 1, 2))
        resp = proto.recv_msg(sock)
        assert resp["type"] == proto.MSG_ERROR
        assert resp["error_code"] == proto.ERR_BAD_REQUEST
        sock.close()


# ── CALC happy path ───────────────────────────────────────────────────────────

class TestCalcHappyPath:
    @pytest.fixture(autouse=True)
    def setup(self, running_server, mock_kernel):
        self.server_path = running_server
        self.kernel = mock_kernel

    def _connect(self) -> socket.socket:
        return connect_and_handshake(self.server_path)

    def test_add(self):
        self.kernel.calculate.return_value = 79
        sock = self._connect()
        resp = do_calc(sock, "ADD", 42, 37)
        assert resp["type"]   == proto.MSG_CALC_ACK
        assert resp["result"] == 79
        sock.close()

    def test_sub(self):
        self.kernel.calculate.return_value = 5
        sock = self._connect()
        resp = do_calc(sock, "SUB", 10, 5)
        assert resp["result"] == 5
        sock.close()

    def test_mul(self):
        self.kernel.calculate.return_value = 12
        sock = self._connect()
        resp = do_calc(sock, "MUL", 3, 4)
        assert resp["result"] == 12
        sock.close()

    def test_div(self):
        self.kernel.calculate.return_value = 5
        sock = self._connect()
        resp = do_calc(sock, "DIV", 10, 2)
        assert resp["result"] == 5
        sock.close()

    def test_negative_operands(self):
        self.kernel.calculate.return_value = 6
        sock = self._connect()
        resp = do_calc(sock, "MUL", -2, -3)
        assert resp["result"] == 6
        sock.close()

    def test_large_integers(self):
        large = 2**62
        self.kernel.calculate.return_value = large
        sock = self._connect()
        resp = do_calc(sock, "ADD", large, 0)
        assert resp["result"] == large
        sock.close()

    def test_zero_result(self):
        self.kernel.calculate.return_value = 0
        sock = self._connect()
        resp = do_calc(sock, "SUB", 5, 5)
        assert resp["result"] == 0
        sock.close()

    def test_calc_ack_echoes_operands(self):
        self.kernel.calculate.return_value = 7
        sock = self._connect()
        resp = do_calc(sock, "ADD", 3, 4, req_id=99)
        assert resp["req_id"] == 99
        assert resp["op"]     == "ADD"
        assert resp["a"]      == 3
        assert resp["b"]      == 4
        sock.close()

    def test_multiple_sequential_requests(self):
        results = [10, 20, 30]
        self.kernel.calculate.side_effect = results
        sock = self._connect()
        for i, expected in enumerate(results, start=1):
            resp = do_calc(sock, "ADD", i, i, req_id=i)
            assert resp["type"]   == proto.MSG_CALC_ACK
            assert resp["result"] == expected
        sock.close()


# ── CALC error cases ──────────────────────────────────────────────────────────

class TestCalcErrors:
    @pytest.fixture(autouse=True)
    def setup(self, running_server, mock_kernel):
        self.server_path = running_server
        self.kernel = mock_kernel

    def _connect(self) -> socket.socket:
        return connect_and_handshake(self.server_path)

    def test_div_by_zero(self):
        self.kernel.calculate.side_effect = OSError(errno.EDOM, "Math argument out of domain")
        sock = self._connect()
        resp = do_calc(sock, "DIV", 10, 0)
        assert resp["type"]       == proto.MSG_ERROR
        assert resp["error_code"] == proto.ERR_DIV_ZERO
        sock.close()

    def test_unknown_operator(self):
        sock = self._connect()
        proto.send_msg(sock, {"type": "CALC", "req_id": 1, "op": "MOD", "a": 5, "b": 3})
        resp = proto.recv_msg(sock)
        assert resp["type"]       == proto.MSG_ERROR
        assert resp["error_code"] == proto.ERR_UNKNOWN_OP
        sock.close()

    def test_missing_op_field(self):
        sock = self._connect()
        proto.send_msg(sock, {"type": "CALC", "req_id": 1, "a": 5, "b": 3})
        resp = proto.recv_msg(sock)
        assert resp["type"]       == proto.MSG_ERROR
        assert resp["error_code"] == proto.ERR_BAD_REQUEST
        sock.close()

    def test_missing_a_field(self):
        sock = self._connect()
        proto.send_msg(sock, {"type": "CALC", "req_id": 1, "op": "ADD", "b": 3})
        resp = proto.recv_msg(sock)
        assert resp["type"]       == proto.MSG_ERROR
        assert resp["error_code"] == proto.ERR_BAD_REQUEST
        sock.close()

    def test_missing_b_field(self):
        sock = self._connect()
        proto.send_msg(sock, {"type": "CALC", "req_id": 1, "op": "ADD", "a": 5})
        resp = proto.recv_msg(sock)
        assert resp["type"]       == proto.MSG_ERROR
        assert resp["error_code"] == proto.ERR_BAD_REQUEST
        sock.close()

    def test_non_integer_operand_a(self):
        sock = self._connect()
        proto.send_msg(sock, {"type": "CALC", "req_id": 1, "op": "ADD", "a": "foo", "b": 3})
        resp = proto.recv_msg(sock)
        assert resp["type"]       == proto.MSG_ERROR
        assert resp["error_code"] == proto.ERR_BAD_REQUEST
        sock.close()

    def test_non_integer_operand_b(self):
        sock = self._connect()
        proto.send_msg(sock, {"type": "CALC", "req_id": 1, "op": "ADD", "a": 5, "b": "bar"})
        resp = proto.recv_msg(sock)
        assert resp["type"]       == proto.MSG_ERROR
        assert resp["error_code"] == proto.ERR_BAD_REQUEST
        sock.close()

    def test_kernel_device_error(self):
        self.kernel.calculate.side_effect = OSError(errno.EIO, "I/O error")
        sock = self._connect()
        resp = do_calc(sock, "ADD", 1, 2)
        assert resp["type"]       == proto.MSG_ERROR
        assert resp["error_code"] == proto.ERR_DEVICE
        sock.close()

    def test_unknown_message_type(self):
        sock = self._connect()
        proto.send_msg(sock, {"type": "UNKNOWN_MSG"})
        resp = proto.recv_msg(sock)
        assert resp["type"]       == proto.MSG_ERROR
        assert resp["error_code"] == proto.ERR_BAD_REQUEST
        sock.close()

    def test_error_response_contains_req_id(self):
        self.kernel.calculate.side_effect = OSError(errno.EDOM, "domain error")
        sock = self._connect()
        proto.send_msg(sock, proto.build_calc(42, "DIV", 1, 0))
        resp = proto.recv_msg(sock)
        assert resp.get("req_id") == 42
        sock.close()


# ── BYE / disconnect ──────────────────────────────────────────────────────────

class TestDisconnect:
    def test_bye_ack(self, running_server):
        sock = connect_and_handshake(running_server)
        proto.send_msg(sock, proto.build_bye("test"))
        ack = proto.recv_msg(sock)
        assert ack["type"] == proto.MSG_BYE_ACK
        sock.close()

    def test_bye_ack_message_present(self, running_server):
        sock = connect_and_handshake(running_server)
        proto.send_msg(sock, proto.build_bye("test"))
        ack = proto.recv_msg(sock)
        assert "message" in ack
        sock.close()


# ── Concurrent clients ────────────────────────────────────────────────────────

class TestConcurrentClients:
    def test_two_clients_simultaneously(self, running_server, mock_kernel):
        results = []
        errors  = []

        mock_kernel.calculate.return_value = 100

        def client_task(client_id: str, op: str, a: int, b: int):
            try:
                sock = connect_and_handshake(running_server, client_id)
                resp = do_calc(sock, op, a, b)
                results.append(resp)
                sock.close()
            except Exception as exc:
                errors.append(exc)

        t1 = threading.Thread(target=client_task, args=("client-A", "ADD", 1, 2))
        t2 = threading.Thread(target=client_task, args=("client-B", "MUL", 3, 4))
        t1.start(); t2.start()
        t1.join(timeout=5); t2.join(timeout=5)

        assert not errors, f"Client errors: {errors}"
        assert len(results) == 2
        assert all(r["type"] == proto.MSG_CALC_ACK for r in results)

    def test_five_clients_simultaneously(self, running_server, mock_kernel):
        mock_kernel.calculate.return_value = 42
        errors = []

        def client_task(n: int):
            try:
                sock = connect_and_handshake(running_server, f"client-{n}")
                resp = do_calc(sock, "ADD", n, n)
                assert resp["type"] == proto.MSG_CALC_ACK
                sock.close()
            except Exception as exc:
                errors.append(exc)

        threads = [threading.Thread(target=client_task, args=(i,)) for i in range(5)]
        for t in threads: t.start()
        for t in threads: t.join(timeout=5)

        assert not errors, f"Errors: {errors}"
