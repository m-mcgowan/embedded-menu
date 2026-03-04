"""Menu client for embedded-menu devices."""

from __future__ import annotations

import json
import time
from typing import Any, Callable, Protocol

from .protocol import CommandInfo, MenuResponse, MenuError, build_request


class LineTransport(Protocol):
    """Abstract line-oriented transport."""

    def write_line(self, line: str) -> None: ...
    def read_line(self, timeout: float) -> str | None: ...


class SerialLineTransport:
    """Serial port transport using pyserial."""

    def __init__(self, port: str, baudrate: int = 115200) -> None:
        import serial  # type: ignore[import-untyped]

        self._serial = serial.Serial(port, baudrate=baudrate, timeout=0.1)
        time.sleep(0.5)  # wait for device reset
        self._drain()

    def write_line(self, line: str) -> None:
        self._serial.write((line + "\n").encode())
        self._serial.flush()

    def read_line(self, timeout: float) -> str | None:
        deadline = time.monotonic() + timeout
        old_timeout = self._serial.timeout
        self._serial.timeout = min(0.1, timeout)
        try:
            while time.monotonic() < deadline:
                raw = self._serial.readline()
                if raw:
                    return raw.decode("utf-8", errors="replace").strip()
            return None
        finally:
            self._serial.timeout = old_timeout

    def close(self) -> None:
        self._serial.close()

    def _drain(self) -> None:
        old_timeout = self._serial.timeout
        self._serial.timeout = 0.1
        while self._serial.readline():
            pass
        self._serial.timeout = old_timeout


class MenuClient:
    """High-level client for embedded-menu devices.

    Supports both JSON transport (structured commands/responses) and
    console transport (plain text). The JSON transport is the primary
    programmatic interface; console is for human-readable output.

    Usage::

        client = MenuClient(port="/dev/cu.usbmodem1234")
        commands = client.discover()
        resp = client.send("add", a=2, b=3)
        print(resp["sum"])  # 5
        client.close()

    Or with a custom transport::

        client = MenuClient(transport=my_transport)
    """

    def __init__(
        self,
        port: str | None = None,
        baudrate: int = 115200,
        timeout: float = 2.0,
        transport: LineTransport | None = None,
        tui: bool = True,
    ) -> None:
        if transport is not None:
            self._transport = transport
            self._owns_transport = False
        elif port is not None:
            self._transport = SerialLineTransport(port, baudrate)
            self._owns_transport = True
        else:
            raise ValueError("either port or transport must be provided")
        self._timeout = timeout
        self._tui_was_disabled = False
        if not tui:
            self.disable_tui()
            self._tui_was_disabled = True

    def disable_tui(self) -> MenuResponse:
        """Disable TUI output on the device for programmatic use."""
        return self.send("tui", enabled=False)

    def enable_tui(self) -> MenuResponse:
        """Re-enable TUI output on the device."""
        return self.send("tui", enabled=True)

    def send(self, cmd: str, **params: Any) -> MenuResponse:
        """Send a JSON command and return the parsed response.

        Raises MenuError if the device returns an error response.
        """
        line = build_request(cmd, **params)
        self._transport.write_line(line)
        resp = self._read_json_response()
        if not resp.ok:
            raise MenuError(resp)
        return resp

    def send_raw(self, line: str) -> MenuResponse:
        """Send a raw JSON line and return the response."""
        self._transport.write_line(line)
        return self._read_json_response()

    def console(self, command: str, timeout: float | None = None) -> list[str]:
        """Send a console command and return text output lines."""
        self._transport.write_line(command)
        return self._read_text_lines(timeout or self._timeout)

    def discover(self) -> list[CommandInfo]:
        """Discover available commands via the help endpoint."""
        resp = self.send("help")
        commands = resp.fields.get("commands", [])
        return [CommandInfo.from_dict(c) for c in commands]

    def get_command_info(self, name: str) -> CommandInfo:
        """Get detailed info for a single command."""
        resp = self.send("help", topic=name)
        return CommandInfo.from_dict(resp.fields)

    def close(self) -> None:
        """Re-enable TUI if we disabled it, then close the transport."""
        if self._tui_was_disabled:
            try:
                self.enable_tui()
            except Exception:
                pass  # best effort — device may already be gone
            self._tui_was_disabled = False
        if self._owns_transport and hasattr(self._transport, "close"):
            self._transport.close()

    def __enter__(self) -> MenuClient:
        return self

    def __exit__(self, *args: Any) -> None:
        self.close()

    def _read_json_response(self) -> MenuResponse:
        """Read lines until we get a valid JSON response.

        Handles TUI prompts that may prefix the JSON on the same line
        (e.g. ``"> {"cmd":"ping","ok":true}"``). Skips lines that
        contain ``{`` but aren't valid JSON (e.g. debug output).
        """
        deadline = time.monotonic() + self._timeout
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            line = self._transport.read_line(min(remaining, 0.5))
            if not line:
                continue
            pos = line.find("{")
            if pos >= 0:
                try:
                    return MenuResponse.from_json_line(line[pos:])
                except (json.JSONDecodeError, ValueError):
                    continue  # not valid JSON, keep reading
        raise TimeoutError(f"no JSON response within {self._timeout}s")

    def _read_text_lines(self, timeout: float) -> list[str]:
        """Read text lines until silence."""
        lines: list[str] = []
        deadline = time.monotonic() + timeout
        silence_threshold = 0.3
        last_data = time.monotonic()

        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            line = self._transport.read_line(min(remaining, 0.1))
            if line:
                if line != ">":
                    lines.append(line)
                last_data = time.monotonic()
            elif lines and (time.monotonic() - last_data) > silence_threshold:
                break

        return lines
