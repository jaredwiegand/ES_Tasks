#!/usr/bin/env python3
"""
client.py  –  mathdev terminal UI client (Python 3.10+)
========================================================

Connects to the mathdev server over a Unix-domain socket and presents
an interactive menu matching the sample output in the spec.

Usage:
    python3 client.py [--socket PATH] [--client-id ID]
"""

from __future__ import annotations

import argparse
import socket
import sys
import uuid
from pathlib import Path
from typing import Any

# Allow running from project root without installing
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "proto"))
import protocol as proto

# ── Terminal colours (graceful fallback on plain terminals) ───────────────────
try:
    import shutil
    _COLOURS = shutil.get_terminal_size().columns > 0
except Exception:
    _COLOURS = False

def _col(code: str, text: str) -> str:
    if not _COLOURS:
        return text
    return f"\033[{code}m{text}\033[0m"

BOLD    = lambda t: _col("1",     t)
GREEN   = lambda t: _col("32",    t)
YELLOW  = lambda t: _col("33",    t)
RED     = lambda t: _col("31",    t)
CYAN    = lambda t: _col("36",    t)
MAGENTA = lambda t: _col("35",    t)

# ── Client class ──────────────────────────────────────────────────────────────

class MathClient:
    def __init__(self, socket_path: str, client_id: str):
        self.socket_path = socket_path
        self.client_id   = client_id
        self._sock: socket.socket | None = None
        self._req_id: int = 0
        self._ops: list[dict[str, Any]] = []  # populated after HELLO_ACK

    # -- connection management -----------------------------------------------

    def connect(self) -> None:
        self._sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._sock.connect(self.socket_path)

    def disconnect(self) -> None:
        if self._sock:
            try:
                proto.send_msg(self._sock, proto.build_bye(self.client_id))
                ack = proto.recv_msg(self._sock)
                if ack and ack.get("type") == proto.MSG_BYE_ACK:
                    pass  # clean disconnect confirmed
            except Exception:
                pass
            self._sock.close()
            self._sock = None

    def _next_req_id(self) -> int:
        self._req_id += 1
        return self._req_id

    # -- protocol operations -------------------------------------------------

    def handshake(self) -> None:
        """Send HELLO, receive HELLO_ACK with available operations."""
        proto.send_msg(self._sock, proto.build_hello(self.client_id))
        msg = proto.recv_msg(self._sock)
        if msg is None or msg.get("type") != proto.MSG_HELLO_ACK:
            raise ConnectionError(f"Expected HELLO_ACK, got: {msg}")
        self._ops = msg.get("ops", proto.AVAILABLE_OPS)

    def request_calc(self, op: str, a: int, b: int) -> dict[str, Any]:
        """
        Send a CALC request and wait for CALC_ACK or ERROR.
        Returns the full response dict.
        """
        req_id = self._next_req_id()
        proto.send_msg(self._sock, proto.build_calc(req_id, op, a, b))
        msg = proto.recv_msg(self._sock)
        if msg is None:
            raise ConnectionError("Server closed connection unexpectedly")
        return msg

    # -- UI helpers ----------------------------------------------------------

    def _print_banner(self) -> None:
        print()
        print(BOLD(CYAN("╔══════════════════════════════════════╗")))
        print(BOLD(CYAN("║       mathdev — kernel math ops      ║")))
        print(BOLD(CYAN("╚══════════════════════════════════════╝")))
        print(f"  Connected as {YELLOW(self.client_id)}")
        print()

    def _print_menu(self) -> None:
        # Build menu from ops announced by server
        op_menu: list[tuple[int, str, str]] = []
        for i, op in enumerate(self._ops, start=1):
            name = op["name"]
            desc = op.get("description", name)
            op_menu.append((i, name, desc))

        for num, name, desc in op_menu:
            print(f"  {BOLD(str(num))}) {desc}")
        exit_num = len(op_menu) + 1
        print(f"  {BOLD(str(exit_num))}) Exit")
        print()
        return op_menu, exit_num

    def _get_int(self, prompt: str) -> int:
        while True:
            raw = input(prompt).strip()
            try:
                return int(raw)
            except ValueError:
                print(RED(f"  ✗ '{raw}' is not a valid integer. Try again."))

    # -- main loop -----------------------------------------------------------

    def run(self) -> None:
        self._print_banner()

        while True:
            op_menu, exit_num = self._print_menu()
            raw = input(BOLD("Enter command: ")).strip()

            try:
                choice = int(raw)
            except ValueError:
                print(RED("  ✗ Invalid choice.\n"))
                continue

            if choice == exit_num:
                print(GREEN("Bye!"))
                break

            if not (1 <= choice <= len(op_menu)):
                print(RED(f"  ✗ Choose between 1 and {exit_num}.\n"))
                continue

            _, op_name, op_desc = op_menu[choice - 1]

            a = self._get_int("Enter operand 1: ")
            b = self._get_int("Enter operand 2: ")

            print(YELLOW("Sending request..."))

            try:
                resp = self.request_calc(op_name, a, b)
            except ConnectionError as exc:
                print(RED(f"  ✗ Connection error: {exc}"))
                break

            rtype = resp.get("type")

            match rtype:
                case proto.MSG_CALC_ACK:
                    print(GREEN("Request OKAY..."))
                    print(YELLOW("Receiving response..."))
                    result = resp["result"]
                    print(BOLD(GREEN(f"Result is {result}!")))

                case proto.MSG_ERROR:
                    code = resp.get("error_code", "UNKNOWN")
                    msg  = resp.get("message", "")
                    print(RED(f"Request FAILED ({code}): {msg}"))

                case _:
                    print(RED(f"Unexpected response type: {rtype!r}"))

            print()


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="mathdev Python client")
    parser.add_argument("--socket",    default=proto.DEFAULT_SOCKET,
                        help="Unix socket path (default: %(default)s)")
    parser.add_argument("--client-id", default=f"python-{uuid.uuid4().hex[:8]}",
                        help="Client identifier string")
    args = parser.parse_args()

    client = MathClient(args.socket, args.client_id)

    try:
        client.connect()
    except (FileNotFoundError, ConnectionRefusedError):
        print(RED(f"Cannot connect to server at {args.socket}"))
        print("Make sure the server is running.")
        sys.exit(1)

    try:
        client.handshake()
        client.run()
    except KeyboardInterrupt:
        print("\nInterrupted.")
    except ConnectionError as exc:
        print(RED(f"Connection lost: {exc}"))
        sys.exit(1)
    finally:
        client.disconnect()


if __name__ == "__main__":
    main()
