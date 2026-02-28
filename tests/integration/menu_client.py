"""Client for embedded-menu devices over serial.

Supports both the JSON transport and the basic serial console.
Uses embedded-bridge's SerialTransport for the connection layer.

Usage:
    from menu_client import MenuClient

    with MenuClient("/dev/cu.usbmodem1234") as client:
        # JSON transport
        resp = client.send("ping")
        assert resp["ok"] is True

        # Console (plain text)
        lines = client.console("help")
        assert any("ping" in l for l in lines)
"""

from __future__ import annotations

import json
import time
from typing import Any

from embedded_bridge.transport.serial import SerialTransport


class MenuClient:
    """Client for embedded-menu JSON serial + console protocols."""

    def __init__(self, port: str, baudrate: int = 115200, timeout: float = 2.0):
        self.transport = SerialTransport(port, baudrate=baudrate)
        self.timeout = timeout

    def connect(self) -> None:
        self.transport.connect()
        time.sleep(0.5)
        self._drain()

    def disconnect(self) -> None:
        self.transport.disconnect()

    # --- JSON transport ---

    def send(self, cmd: str, **params: Any) -> dict:
        """Send a JSON command and return the parsed JSON response."""
        request = {"cmd": cmd, **params}
        line = json.dumps(request, separators=(",", ":")) + "\n"
        self._write(line)
        return self._read_json_response()

    def send_raw(self, line: str) -> dict:
        """Send a raw line and return the JSON response."""
        if not line.endswith("\n"):
            line += "\n"
        self._write(line)
        return self._read_json_response()

    # --- Console (plain text) ---

    def console(self, command: str, timeout: float | None = None) -> list[str]:
        """Send a console command and return all text output lines.

        Args:
            command: Plain text command, e.g. "help" or "add a 2 b 3"
            timeout: Override default timeout for this command.

        Returns:
            List of non-empty output lines (prompt stripped).
        """
        self._drain()
        self._write(command + "\n")
        return self._read_text_lines(timeout or self.timeout)

    # --- Internal ---

    def _write(self, data: str) -> None:
        self.transport._serial.write(data.encode())
        self.transport._serial.flush()

    def _read_json_response(self) -> dict:
        """Read lines until we get a valid JSON response."""
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            line = self.transport._serial.readline()
            if not line:
                continue
            text = line.decode("utf-8", errors="replace").strip()
            if not text:
                continue
            try:
                return json.loads(text)
            except json.JSONDecodeError:
                continue
        raise TimeoutError(f"No JSON response within {self.timeout}s")

    def _read_text_lines(self, timeout: float) -> list[str]:
        """Read text lines until silence (no data for a short period)."""
        lines: list[str] = []
        deadline = time.monotonic() + timeout
        silence_threshold = 0.3  # stop after 300ms of no new data
        last_data = time.monotonic()

        old_timeout = self.transport._serial.timeout
        self.transport._serial.timeout = 0.1  # wait up to 100ms per readline

        try:
            while time.monotonic() < deadline:
                line = self.transport._serial.readline()
                if line:
                    text = line.decode("utf-8", errors="replace").strip()
                    # Skip prompt lines and empty lines
                    if text and text != ">":
                        lines.append(text)
                    last_data = time.monotonic()
                elif lines and (time.monotonic() - last_data) > silence_threshold:
                    break
        finally:
            self.transport._serial.timeout = old_timeout

        return lines

    def _drain(self) -> None:
        old_timeout = self.transport._serial.timeout
        self.transport._serial.timeout = 0.1
        while self.transport._serial.readline():
            pass
        self.transport._serial.timeout = old_timeout

    def __enter__(self) -> MenuClient:
        self.connect()
        return self

    def __exit__(self, *args: Any) -> None:
        self.disconnect()
