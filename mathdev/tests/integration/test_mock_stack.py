"""
test_mock_stack.py — integration tests using mock_server.py
============================================================
Tests the full client→server→(mock kernel)→response stack
without requiring a real /dev/mathdev device.

These tests are safe to run in CI on any Linux/Mac machine.
"""

from __future__ import annotations

import socket
import sys
import threading
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "proto"))
sys.path.insert(0, str(ROOT / "server"))

import protocol as proto
from tests.conftest import connect_and_handshake


def do_calc(sock, op, a, b, req_id=1):
    proto.send_msg(sock, proto.build_calc(req_id, op, a, b))
    return proto.recv_msg(sock)


# ── Full handshake + calculate round-trip ────────────────────────────────────

class TestFullStack:
    def test_add_round_trip(self, running_server, mock_kernel):
        mock_kernel.calculate.return_value = 79
        sock = connect_and_handshake(running_server)
        resp = do_calc(sock, "ADD", 42, 37)
        assert resp["type"]   == proto.MSG_CALC_ACK
        assert resp["result"] == 79
        sock.close()

    def test_sub_round_trip(self, running_server, mock_kernel):
        mock_kernel.calculate.return_value = -5
        sock = connect_and_handshake(running_server)
        resp = do_calc(sock, "SUB", 0, 5)
        assert resp["result"] == -5
        sock.close()

    def test_mul_round_trip(self, running_server, mock_kernel):
        mock_kernel.calculate.return_value = 100
        sock = connect_and_handshake(running_server)
        resp = do_calc(sock, "MUL", 10, 10)
        assert resp["result"] == 100
        sock.close()

    def test_div_round_trip(self, running_server, mock_kernel):
        mock_kernel.calculate.return_value = 3
        sock = connect_and_handshake(running_server)
        resp = do_calc(sock, "DIV", 9, 3)
        assert resp["result"] == 3
        sock.close()

    def test_service_announcement_in_hello_ack(self, running_server):
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(running_server)
        proto.send_msg(sock, proto.build_hello("test"))
        ack = proto.recv_msg(sock)
        assert ack["type"] == proto.MSG_HELLO_ACK
        ops = {op["name"] for op in ack["ops"]}
        assert ops == {"ADD", "SUB", "MUL", "DIV"}
        sock.close()

    def test_full_session_sequence(self, running_server, mock_kernel):
        """HELLO → CALC × 3 → BYE complete session."""
        mock_kernel.calculate.side_effect = [30, -1, 6]
        sock = connect_and_handshake(running_server)

        r1 = do_calc(sock, "ADD", 10, 20, req_id=1)
        r2 = do_calc(sock, "SUB", 0, 1,  req_id=2)
        r3 = do_calc(sock, "MUL", 2, 3,  req_id=3)

        assert r1["result"] == 30
        assert r2["result"] == -1
        assert r3["result"] == 6

        proto.send_msg(sock, proto.build_bye("test"))
        bye_ack = proto.recv_msg(sock)
        assert bye_ack["type"] == proto.MSG_BYE_ACK
        sock.close()

    def test_req_id_preserved(self, running_server, mock_kernel):
        mock_kernel.calculate.return_value = 0
        sock = connect_and_handshake(running_server)
        for req_id in [1, 100, 9999]:
            resp = do_calc(sock, "ADD", 0, 0, req_id=req_id)
            assert resp["req_id"] == req_id
        sock.close()


# ── Error propagation end-to-end ──────────────────────────────────────────────

class TestErrorPropagation:
    def test_div_zero_error_code(self, running_server, mock_kernel):
        import errno
        mock_kernel.calculate.side_effect = OSError(errno.EDOM, "domain")
        sock = connect_and_handshake(running_server)
        resp = do_calc(sock, "DIV", 5, 0)
        assert resp["type"]       == proto.MSG_ERROR
        assert resp["error_code"] == proto.ERR_DIV_ZERO
        sock.close()

    def test_unknown_op_error_code(self, running_server):
        sock = connect_and_handshake(running_server)
        proto.send_msg(sock, {"type": "CALC", "req_id": 1, "op": "POW", "a": 2, "b": 8})
        resp = proto.recv_msg(sock)
        assert resp["error_code"] == proto.ERR_UNKNOWN_OP
        sock.close()

    def test_connection_continues_after_error(self, running_server, mock_kernel):
        """Server should keep the connection alive after returning an error."""
        import errno
        mock_kernel.calculate.side_effect = [
            OSError(errno.EDOM, "domain"),  # first call: error
            42,                              # second call: success
        ]
        sock = connect_and_handshake(running_server)

        # First request — error
        resp1 = do_calc(sock, "DIV", 1, 0, req_id=1)
        assert resp1["type"] == proto.MSG_ERROR

        # Second request — success, same connection
        resp2 = do_calc(sock, "ADD", 40, 2, req_id=2)
        assert resp2["type"]   == proto.MSG_CALC_ACK
        assert resp2["result"] == 42
        sock.close()


# ── Concurrent clients integration ───────────────────────────────────────────

class TestConcurrentIntegration:
    def test_ten_concurrent_clients(self, running_server, mock_kernel):
        mock_kernel.calculate.return_value = 1
        errors  = []
        results = []
        lock    = threading.Lock()

        def run_client(n):
            try:
                sock = connect_and_handshake(running_server, f"client-{n}")
                resp = do_calc(sock, "ADD", n, 0)
                with lock:
                    results.append(resp)
                proto.send_msg(sock, proto.build_bye(f"client-{n}"))
                proto.recv_msg(sock)
                sock.close()
            except Exception as exc:
                with lock:
                    errors.append(str(exc))

        threads = [threading.Thread(target=run_client, args=(i,)) for i in range(10)]
        for t in threads: t.start()
        for t in threads: t.join(timeout=10)

        assert not errors, f"Errors: {errors}"
        assert len(results) == 10
        assert all(r["type"] == proto.MSG_CALC_ACK for r in results)
