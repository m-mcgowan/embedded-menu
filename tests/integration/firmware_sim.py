"""PTY-based firmware simulator for hardware-neutral integration testing.

Implements the same command behaviour as examples/all_interfaces so that
test_json_commands.py runs against this instead of a real device when no
--port is provided.
"""

from __future__ import annotations

import json
import os
import pty
import threading
import time


class FirmwareSimulator:
    """Simulates the all_interfaces firmware over a PTY pair.

    Usage::

        with FirmwareSimulator() as sim:
            client = MenuClient(sim.port)
            ...
    """

    def __init__(self) -> None:
        self._master_fd, slave_fd = pty.openpty()
        self.port: str = os.ttyname(slave_fd)
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._uptime_start = time.monotonic()

    def start(self) -> "FirmwareSimulator":
        self._thread.start()
        return self

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=2)
        try:
            os.close(self._master_fd)
        except OSError:
            pass

    def __enter__(self) -> "FirmwareSimulator":
        return self.start()

    def __exit__(self, *args: object) -> None:
        self.stop()

    # ── I/O loop ─────────────────────────────────────────────────────────────

    def _run(self) -> None:
        buf = b""
        while not self._stop.is_set():
            try:
                chunk = os.read(self._master_fd, 256)
            except OSError:
                break
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = raw.rstrip(b"\r").decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                response = self._process_line(line)
                if response is not None:
                    os.write(self._master_fd, (response + "\n").encode())

    def _process_line(self, line: str) -> str | None:
        if line.startswith("{"):
            return self._process_json(line)
        else:
            return self._process_console(line)

    # ── JSON transport ────────────────────────────────────────────────────────

    def _process_json(self, line: str) -> str:
        try:
            req = json.loads(line)
        except json.JSONDecodeError:
            return json.dumps({"error": "parse error"})

        cmd = req.get("cmd", "")
        if not cmd:
            return json.dumps({"error": "missing cmd"})

        return json.dumps(self._dispatch_json(cmd, req))

    def _dispatch_json(self, cmd: str, req: dict) -> dict:
        if cmd == "ping":
            return {"cmd": "ping", "ok": True}
        elif cmd in ("echo", "e"):
            return {"cmd": "echo", "msg": req.get("msg", "")}
        elif cmd == "add":
            return {"cmd": "add", "sum": req.get("a", 0) + req.get("b", 0)}
        elif cmd == "led":
            return {"cmd": "led", "state": req.get("state", False)}
        elif cmd == "info":
            return {
                "cmd": "info",
                "board": "sim",
                "firmware": "embedded-menu-example",
                "version": "0.1.0",
                "uptime_ms": int((time.monotonic() - self._uptime_start) * 1000),
            }
        elif cmd == "sleep":
            return {"cmd": "sleep", "sleeping": req.get("ms", 5000)}
        elif cmd == "tui":
            return {"cmd": "tui", "enabled": req.get("enabled", True)}
        elif cmd == "help":
            return self._help_response(req.get("topic"))
        else:
            return {"cmd": cmd, "error": "not found"}

    def _help_response(self, topic: str | None) -> dict:
        schema = [
            {"name": "ping", "help": "Echo a ping", "group": None,
             "aliases": [], "params": []},
            {"name": "echo", "help": "Echo back a message", "group": None,
             "aliases": ["e"],
             "params": [{"name": "msg", "type": "string",
                         "required": True, "help": "Message to echo", "default": ""}]},
            {"name": "led", "help": "Control built-in LED", "group": "hw",
             "aliases": [],
             "params": [{"name": "state", "type": "bool", "required": False,
                         "help": "true = on, false = off", "default": "false"}]},
            {"name": "add", "help": "Add two numbers", "group": "math",
             "aliases": [],
             "params": [{"name": "a", "type": "int", "required": False, "default": "0"},
                        {"name": "b", "type": "int", "required": False, "default": "0"}]},
            {"name": "info", "help": "Device info", "group": "system",
             "aliases": [], "params": []},
            {"name": "sleep", "help": "Sleep for N ms (deep sleep)", "group": "system",
             "aliases": [],
             "params": [{"name": "ms", "type": "int", "required": False, "default": "5000"}]},
        ]
        if topic:
            entry = next((e for e in schema if e["name"] == topic), None)
            if not entry:
                return {"cmd": "help", "error": "not found"}
            return {"cmd": "help", **entry}
        return {"cmd": "help", "commands": schema}

    # ── Console transport ─────────────────────────────────────────────────────

    def _process_console(self, line: str) -> str | None:
        parts = line.split()
        if not parts:
            return None
        cmd = parts[0]
        # Parse key-value pairs: "add a 5 b 3" → {"a": "5", "b": "3"}
        args: dict[str, str] = {}
        i = 1
        while i + 1 < len(parts):
            args[parts[i]] = parts[i + 1]
            i += 2
        # Positional fallback for commands that take a bare value
        positional = parts[1] if len(parts) > 1 else ""

        if cmd == "ping":
            return "pong"
        elif cmd in ("echo", "e"):
            return f"msg: {args.get('msg', positional)}"
        elif cmd == "add":
            a = int(args.get("a", 0))
            b = int(args.get("b", 0))
            return str(a + b)
        elif cmd == "led":
            state = args.get("state", positional)
            return f"LED {'on' if state in ('on', 'true', '1') else 'off'}"
        elif cmd == "info":
            uptime = int((time.monotonic() - self._uptime_start) * 1000)
            return (
                "board:    sim\r\n"
                "firmware: embedded-menu-example\r\n"
                "version:  0.1.0\r\n"
                f"uptime:   {uptime} ms"
            )
        elif cmd == "help":
            if positional:
                return f"  {positional}   (command)"
            return (
                "Commands:\r\n"
                "  ping             Echo a ping\r\n"
                "  echo (e)         Echo back a message\r\n"
                "  led              Control built-in LED\r\n"
                "  add              Add two numbers\r\n"
                "  info             Device info\r\n"
                "  sleep            Sleep for N ms"
            )
        elif cmd == "sleep":
            ms = int(args.get("ms", positional or "5000"))
            return f"deep sleep {ms} ms"
        else:
            return f"unknown: {cmd}"
