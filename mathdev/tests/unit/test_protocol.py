"""
test_protocol.py — unit tests for proto/protocol.py
====================================================
Tests:
  - OpCode enum values match kernel constants
  - All message builder functions
  - send_msg / recv_msg framing round-trip
  - recv_msg on clean EOF returns None
  - recv_msg on truncated data raises ConnectionError
  - recv_msg on invalid JSON raises ConnectionError
  - Large message round-trip
  - Zero-length message round-trip
"""

from __future__ import annotations

import json
import socket
import struct
import sys
import threading
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "proto"))
import protocol as proto


# ── OpCode constants ──────────────────────────────────────────────────────────

class TestOpCode:
    def test_add_value(self):
        assert proto.OpCode.ADD == 1

    def test_sub_value(self):
        assert proto.OpCode.SUB == 2

    def test_mul_value(self):
        assert proto.OpCode.MUL == 3

    def test_div_value(self):
        assert proto.OpCode.DIV == 4

    def test_op_name_map_complete(self):
        assert set(proto.OP_NAME_MAP.keys()) == {"ADD", "SUB", "MUL", "DIV"}

    def test_op_code_map_complete(self):
        assert set(proto.OP_CODE_MAP.keys()) == {1, 2, 3, 4}

    def test_round_trip_name_to_code(self):
        for op in proto.OpCode:
            assert proto.OP_NAME_MAP[op.name] == op.value

    def test_round_trip_code_to_name(self):
        for op in proto.OpCode:
            assert proto.OP_CODE_MAP[op.value] == op.name


# ── Message type constants ────────────────────────────────────────────────────

class TestMessageConstants:
    def test_hello(self):       assert proto.MSG_HELLO     == "HELLO"
    def test_hello_ack(self):   assert proto.MSG_HELLO_ACK == "HELLO_ACK"
    def test_calc(self):        assert proto.MSG_CALC      == "CALC"
    def test_calc_ack(self):    assert proto.MSG_CALC_ACK  == "CALC_ACK"
    def test_error(self):       assert proto.MSG_ERROR     == "ERROR"
    def test_bye(self):         assert proto.MSG_BYE       == "BYE"
    def test_bye_ack(self):     assert proto.MSG_BYE_ACK   == "BYE_ACK"


# ── Error code constants ──────────────────────────────────────────────────────

class TestErrorCodes:
    def test_unknown_op(self):   assert proto.ERR_UNKNOWN_OP  == "ERR_UNKNOWN_OP"
    def test_div_zero(self):     assert proto.ERR_DIV_ZERO    == "ERR_DIV_ZERO"
    def test_bad_request(self):  assert proto.ERR_BAD_REQUEST == "ERR_BAD_REQUEST"
    def test_device(self):       assert proto.ERR_DEVICE      == "ERR_DEVICE"
    def test_internal(self):     assert proto.ERR_INTERNAL    == "ERR_INTERNAL"


# ── Message builders ──────────────────────────────────────────────────────────

class TestBuildHello:
    def test_type(self):
        m = proto.build_hello("cli-1")
        assert m["type"] == "HELLO"

    def test_version(self):
        m = proto.build_hello("cli-1")
        assert m["version"] == proto.PROTOCOL_VERSION

    def test_client_id(self):
        m = proto.build_hello("my-client")
        assert m["client_id"] == "my-client"


class TestBuildHelloAck:
    def test_type(self):
        m = proto.build_hello_ack()
        assert m["type"] == "HELLO_ACK"

    def test_version(self):
        m = proto.build_hello_ack()
        assert m["version"] == proto.PROTOCOL_VERSION

    def test_server_id(self):
        m = proto.build_hello_ack()
        assert m["server_id"] == proto.SERVER_ID

    def test_default_ops(self):
        m = proto.build_hello_ack()
        assert m["ops"] == proto.AVAILABLE_OPS

    def test_custom_ops(self):
        custom = [{"op_code": 1, "name": "ADD", "description": "test"}]
        m = proto.build_hello_ack(ops=custom)
        assert m["ops"] == custom

    def test_ops_has_four_entries(self):
        m = proto.build_hello_ack()
        assert len(m["ops"]) == 4


class TestBuildCalc:
    def test_type(self):
        m = proto.build_calc(1, "ADD", 10, 20)
        assert m["type"] == "CALC"

    def test_req_id(self):
        m = proto.build_calc(42, "ADD", 10, 20)
        assert m["req_id"] == 42

    def test_op(self):
        m = proto.build_calc(1, "MUL", 3, 4)
        assert m["op"] == "MUL"

    def test_operands(self):
        m = proto.build_calc(1, "ADD", -5, 100)
        assert m["a"] == -5
        assert m["b"] == 100


class TestBuildCalcAck:
    def test_type(self):
        m = proto.build_calc_ack(1, "ADD", 2, 3, 5)
        assert m["type"] == "CALC_ACK"

    def test_all_fields(self):
        m = proto.build_calc_ack(7, "DIV", 10, 2, 5)
        assert m["req_id"] == 7
        assert m["op"]     == "DIV"
        assert m["a"]      == 10
        assert m["b"]      == 2
        assert m["result"] == 5

    def test_negative_result(self):
        m = proto.build_calc_ack(1, "SUB", 3, 10, -7)
        assert m["result"] == -7


class TestBuildError:
    def test_type(self):
        m = proto.build_error(1, proto.ERR_DIV_ZERO, "div by zero")
        assert m["type"] == "ERROR"

    def test_with_req_id(self):
        m = proto.build_error(5, proto.ERR_DEVICE, "oops")
        assert m["req_id"] == 5

    def test_without_req_id(self):
        m = proto.build_error(None, proto.ERR_INTERNAL, "oops")
        assert "req_id" not in m

    def test_error_code(self):
        m = proto.build_error(1, proto.ERR_UNKNOWN_OP, "bad op")
        assert m["error_code"] == proto.ERR_UNKNOWN_OP

    def test_message(self):
        m = proto.build_error(1, proto.ERR_BAD_REQUEST, "missing field")
        assert m["message"] == "missing field"


class TestBuildBye:
    def test_type(self):
        m = proto.build_bye("cli-1")
        assert m["type"] == "BYE"

    def test_client_id(self):
        m = proto.build_bye("my-id")
        assert m["client_id"] == "my-id"


class TestBuildByeAck:
    def test_type(self):
        m = proto.build_bye_ack()
        assert m["type"] == "BYE_ACK"

    def test_message_present(self):
        m = proto.build_bye_ack()
        assert "message" in m


# ── Framing: send_msg / recv_msg ─────────────────────────────────────────────

class TestFraming:
    """All framing tests use a real socketpair for authentic I/O."""

    @pytest.fixture(autouse=True)
    def sockets(self):
        self.a, self.b = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
        yield
        self.a.close()
        self.b.close()

    def test_simple_round_trip(self):
        msg = {"type": "HELLO", "version": "1.0", "client_id": "test"}
        proto.send_msg(self.a, msg)
        received = proto.recv_msg(self.b)
        assert received == msg

    def test_all_field_types(self):
        msg = {"str": "hello", "int": 42, "neg": -7, "list": [1, 2, 3]}
        proto.send_msg(self.a, msg)
        assert proto.recv_msg(self.b) == msg

    def test_large_message(self):
        msg = {"data": "x" * 100_000}
        proto.send_msg(self.a, msg)
        assert proto.recv_msg(self.b) == msg

    def test_multiple_messages_in_sequence(self):
        messages = [
            proto.build_hello("cli"),
            proto.build_calc(1, "ADD", 1, 2),
            proto.build_bye("cli"),
        ]
        for m in messages:
            proto.send_msg(self.a, m)
        for m in messages:
            assert proto.recv_msg(self.b) == m

    def test_clean_eof_returns_none(self):
        self.a.close()
        result = proto.recv_msg(self.b)
        assert result is None

    def test_truncated_header_raises(self):
        # Send only 2 bytes of a 4-byte header
        self.a.send(b"\x00\x00")
        self.a.close()
        with pytest.raises(ConnectionError):
            proto.recv_msg(self.b)

    def test_truncated_body_raises(self):
        # Send header saying 100 bytes, but only send 10
        self.a.send(struct.pack(">I", 100))
        self.a.send(b"x" * 10)
        self.a.close()
        with pytest.raises(ConnectionError):
            proto.recv_msg(self.b)

    def test_invalid_json_raises(self):
        data = b"not valid json!!!"
        self.a.send(struct.pack(">I", len(data)))
        self.a.send(data)
        with pytest.raises(ConnectionError):
            proto.recv_msg(self.b)

    def test_length_prefix_is_big_endian(self):
        msg = {"type": "TEST"}
        proto.send_msg(self.a, msg)
        raw = self.b.recv(4)
        length = struct.unpack(">I", raw)[0]
        body = self.b.recv(length)
        assert json.loads(body) == msg

    def test_unicode_round_trip(self):
        msg = {"text": "héllo wörld — 日本語"}
        proto.send_msg(self.a, msg)
        assert proto.recv_msg(self.b) == msg

    def test_negative_integers_round_trip(self):
        msg = proto.build_calc_ack(1, "SUB", -999, 1, -1000)
        proto.send_msg(self.a, msg)
        received = proto.recv_msg(self.b)
        assert received["result"] == -1000


# ── AVAILABLE_OPS ─────────────────────────────────────────────────────────────

class TestAvailableOps:
    def test_four_ops(self):
        assert len(proto.AVAILABLE_OPS) == 4

    def test_all_have_op_code(self):
        for op in proto.AVAILABLE_OPS:
            assert "op_code" in op

    def test_all_have_name(self):
        for op in proto.AVAILABLE_OPS:
            assert "name" in op

    def test_all_have_description(self):
        for op in proto.AVAILABLE_OPS:
            assert "description" in op

    def test_op_codes_match_enum(self):
        names = {op["name"]: op["op_code"] for op in proto.AVAILABLE_OPS}
        assert names["ADD"] == proto.OpCode.ADD
        assert names["SUB"] == proto.OpCode.SUB
        assert names["MUL"] == proto.OpCode.MUL
        assert names["DIV"] == proto.OpCode.DIV
