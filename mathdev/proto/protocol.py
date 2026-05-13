"""
protocol.py  –  mathdev IPC protocol (shared by server and clients)
===================================================================

All messages are length-prefixed JSON sent over a Unix-domain stream socket.

Wire framing
------------
Every message is preceded by a 4-byte big-endian uint32 that gives the byte
length of the JSON payload that follows:

    [ 4 bytes big-endian uint32 length ][ UTF-8 JSON payload ]

Every message has at minimum a "type" field.

Message types (client → server)
--------------------------------
  HELLO       – Handshake / service-announcement request
  CALC        – Perform a math operation
  BYE         – Graceful disconnect

Message types (server → client)
--------------------------------
  HELLO_ACK   – Handshake response; includes list of available operations
  CALC_ACK    – Successful calculation; includes result
  ERROR       – Failure; includes error_code + human-readable message
  BYE_ACK     – Server acknowledges client disconnect

Error codes
-----------
  ERR_UNKNOWN_OP    – Operator not recognised
  ERR_DIV_ZERO      – Division by zero
  ERR_BAD_REQUEST   – Malformed JSON / missing fields
  ERR_DEVICE        – Kernel device returned an error
  ERR_INTERNAL      – Unexpected server-side error

Operator identifiers  (must match kernel MATH_OP_* constants)
--------------
  ADD  = 1
  SUB  = 2
  MUL  = 3
  DIV  = 4

Wire examples (JSON payload only; each is preceded by its 4-byte length header)
-------------
Client → Server:
  {"type": "HELLO", "version": "1.0", "client_id": "python-cli-1"}

Server → Client:
  {"type": "HELLO_ACK", "version": "1.0", "server_id": "mathdev-server",
   "ops": [
     {"op_code": 1, "name": "ADD", "description": "Add two signed integers"},
     {"op_code": 2, "name": "SUB", "description": "Subtract two signed integers"},
     {"op_code": 3, "name": "MUL", "description": "Multiply two signed integers"},
     {"op_code": 4, "name": "DIV", "description": "Divide two signed integers"}
   ]}

Client → Server:
  {"type": "CALC", "req_id": 1, "op": "ADD", "a": 42, "b": 37}

Server → Client (success):
  {"type": "CALC_ACK", "req_id": 1, "op": "ADD", "a": 42, "b": 37, "result": 79}

Server → Client (error):
  {"type": "ERROR", "req_id": 1, "error_code": "ERR_DIV_ZERO",
   "message": "Division by zero is undefined"}

Client → Server:
  {"type": "BYE", "client_id": "python-cli-1"}

Server → Client:
  {"type": "BYE_ACK", "message": "Goodbye!"}
"""

from __future__ import annotations

import json
import socket
import struct
from enum import IntEnum
from typing import Any

# ── Protocol version ──────────────────────────────────────────────────────────
PROTOCOL_VERSION = "1.0"
SERVER_ID        = "mathdev-server"
DEFAULT_SOCKET   = "/tmp/mathdev.sock"

# ── Operator codes (mirror kernel MATH_OP_*) ─────────────────────────────────
class OpCode(IntEnum):
    ADD = 1
    SUB = 2
    MUL = 3
    DIV = 4

OP_NAME_MAP: dict[str, int] = {op.name: op.value for op in OpCode}
OP_CODE_MAP: dict[int, str] = {op.value: op.name for op in OpCode}

# ── Message type constants ────────────────────────────────────────────────────
MSG_HELLO     = "HELLO"
MSG_HELLO_ACK = "HELLO_ACK"
MSG_CALC      = "CALC"
MSG_CALC_ACK  = "CALC_ACK"
MSG_ERROR     = "ERROR"
MSG_BYE       = "BYE"
MSG_BYE_ACK   = "BYE_ACK"

# ── Error codes ───────────────────────────────────────────────────────────────
ERR_UNKNOWN_OP  = "ERR_UNKNOWN_OP"
ERR_DIV_ZERO    = "ERR_DIV_ZERO"
ERR_BAD_REQUEST = "ERR_BAD_REQUEST"
ERR_DEVICE      = "ERR_DEVICE"
ERR_INTERNAL    = "ERR_INTERNAL"

# ── Service announcement (authoritative list kept here; server uses it too) ───
AVAILABLE_OPS: list[dict[str, Any]] = [
    {"op_code": OpCode.ADD, "name": "ADD", "description": "Add two signed integers"},
    {"op_code": OpCode.SUB, "name": "SUB", "description": "Subtract two signed integers"},
    {"op_code": OpCode.MUL, "name": "MUL", "description": "Multiply two signed integers"},
    {"op_code": OpCode.DIV, "name": "DIV", "description": "Divide two signed integers"},
]

# ── Low-level framing helpers ─────────────────────────────────────────────────

def send_msg(sock: socket.socket, msg: dict[str, Any]) -> None:
    """Serialise *msg* as JSON and send it with a 4-byte length prefix."""
    data = json.dumps(msg).encode("utf-8")
    header = struct.pack(">I", len(data))  # big-endian uint32 length
    sock.sendall(header + data)


def recv_msg(sock: socket.socket) -> dict[str, Any] | None:
    """
    Read one length-prefixed JSON message from *sock*.
    Returns the decoded dict, or None if the connection was closed cleanly.
    Raises ConnectionError on truncated reads or invalid JSON.
    """
    # Read 4-byte length header
    raw_len = _recv_exactly(sock, 4)
    if raw_len is None:
        return None  # peer closed connection

    (length,) = struct.unpack(">I", raw_len)
    if length == 0:
        return {}

    raw_data = _recv_exactly(sock, length)
    if raw_data is None:
        raise ConnectionError("Connection closed while reading message body")

    try:
        return json.loads(raw_data.decode("utf-8"))
    except json.JSONDecodeError as exc:
        raise ConnectionError(f"Invalid JSON received: {exc}") from exc


def _recv_exactly(sock: socket.socket, n: int) -> bytes | None:
    """Read exactly *n* bytes from *sock*.  Returns None on clean EOF."""
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            if not buf:
                return None   # clean EOF at message boundary
            raise ConnectionError(f"Truncated read: expected {n} bytes, got {len(buf)}")
        buf += chunk
    return buf

# ── Message builder helpers ───────────────────────────────────────────────────

def build_hello(client_id: str) -> dict[str, Any]:
    return {"type": MSG_HELLO, "version": PROTOCOL_VERSION, "client_id": client_id}


def build_hello_ack(ops: list[dict] | None = None) -> dict[str, Any]:
    return {
        "type":      MSG_HELLO_ACK,
        "version":   PROTOCOL_VERSION,
        "server_id": SERVER_ID,
        "ops":       ops if ops is not None else AVAILABLE_OPS,
    }


def build_calc(req_id: int, op: str, a: int, b: int) -> dict[str, Any]:
    return {"type": MSG_CALC, "req_id": req_id, "op": op, "a": a, "b": b}


def build_calc_ack(req_id: int, op: str, a: int, b: int, result: int) -> dict[str, Any]:
    return {
        "type":    MSG_CALC_ACK,
        "req_id":  req_id,
        "op":      op,
        "a":       a,
        "b":       b,
        "result":  result,
    }


def build_error(req_id: int | None, error_code: str, message: str) -> dict[str, Any]:
    msg: dict[str, Any] = {
        "type":       MSG_ERROR,
        "error_code": error_code,
        "message":    message,
    }
    if req_id is not None:
        msg["req_id"] = req_id
    return msg


def build_bye(client_id: str) -> dict[str, Any]:
    return {"type": MSG_BYE, "client_id": client_id}


def build_bye_ack() -> dict[str, Any]:
    return {"type": MSG_BYE_ACK, "message": "Goodbye!"}
