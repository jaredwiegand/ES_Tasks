from __future__ import annotations

import os
import socket
import sys
import threading
import time
from pathlib import Path
from unittest.mock import MagicMock, patch

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "proto"))
sys.path.insert(0, str(ROOT / "server"))

import protocol as proto
from server import MathServer, KernelInterface


@pytest.fixture
def socket_pair():
    a, b = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
    yield a, b
    a.close()
    b.close()


@pytest.fixture
def mock_kernel() -> MagicMock:
    k = MagicMock(spec=KernelInterface)
    k.query_ops.return_value = [
        {"op_code": 1, "name": "ADD", "description": "Add two signed integers"},
        {"op_code": 2, "name": "SUB", "description": "Subtract two signed integers"},
        {"op_code": 3, "name": "MUL", "description": "Multiply two signed integers"},
        {"op_code": 4, "name": "DIV", "description": "Divide two signed integers"},
    ]
    k.calculate.return_value = 0
    return k


@pytest.fixture
def server_socket_path(tmp_path: Path) -> str:
    return str(tmp_path / "test_mathdev.sock")


@pytest.fixture
def running_server(server_socket_path, mock_kernel):
    server = MathServer(server_socket_path, "/dev/null")

    # Patch signal so it works from a non-main thread
    signal_patcher = patch("server.signal.signal")
    signal_patcher.start()

    # Patch KernelInterface — keep a reference to the instance so tests
    # can update return_value / side_effect on mock_kernel and have it
    # flow through to the running server
    ki_patcher = patch("server.KernelInterface")
    MockKI = ki_patcher.start()
    instance = MockKI.return_value.__enter__.return_value

    # Forward all calls on instance to mock_kernel so test fixtures work
    instance.query_ops.side_effect  = lambda: mock_kernel.query_ops()
    instance.calculate.side_effect  = lambda op, a, b: mock_kernel.calculate(op, a, b)

    t = threading.Thread(target=server.run, daemon=True)
    t.start()

    deadline = time.time() + 5.0
    while not os.path.exists(server_socket_path):
        assert time.time() < deadline, "Server did not start in time"
        time.sleep(0.05)

    yield server_socket_path

    server._running = False
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(server_socket_path)
        s.close()
    except Exception:
        pass
    t.join(timeout=3.0)
    ki_patcher.stop()
    signal_patcher.stop()


def connect_and_handshake(socket_path: str, client_id: str = "test-client") -> socket.socket:
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(socket_path)
    proto.send_msg(sock, proto.build_hello(client_id))
    ack = proto.recv_msg(sock)
    assert ack is not None
    assert ack["type"] == proto.MSG_HELLO_ACK
    return sock
