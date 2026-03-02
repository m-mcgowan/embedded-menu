"""Fixtures for embedded-menu Python tests."""

from __future__ import annotations

import os
import select
import subprocess
import time
from pathlib import Path

import pytest

from embedded_menu.client import MenuClient


class SubprocessTransport:
    """LineTransport that drives a C++ server over stdin/stdout pipes."""

    def __init__(self, exe_path: str | Path) -> None:
        self._proc = subprocess.Popen(
            [str(exe_path)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self._fd = self._proc.stdout.fileno()
        os.set_blocking(self._fd, False)
        self._buf = b""

    def write_line(self, line: str) -> None:
        assert self._proc.stdin is not None
        self._proc.stdin.write((line + "\n").encode())
        self._proc.stdin.flush()

    def read_line(self, timeout: float) -> str | None:
        """Read a newline-terminated line, or return None on timeout.

        Uses non-blocking reads so partial lines (like "> " prompts
        without a trailing newline) don't block forever.
        """
        deadline = time.monotonic() + timeout
        while True:
            # Check if we already have a complete line buffered
            idx = self._buf.find(b"\n")
            if idx >= 0:
                line = self._buf[:idx]
                self._buf = self._buf[idx + 1:]
                return line.decode("utf-8", errors="replace").rstrip("\r")

            remaining = deadline - time.monotonic()
            if remaining <= 0:
                # Timeout — return any partial data as a line
                if self._buf:
                    line = self._buf
                    self._buf = b""
                    return line.decode("utf-8", errors="replace").rstrip("\r")
                return None

            ready, _, _ = select.select([self._fd], [], [], min(remaining, 0.1))
            if ready:
                chunk = os.read(self._fd, 4096)
                if not chunk:
                    # EOF
                    if self._buf:
                        line = self._buf
                        self._buf = b""
                        return line.decode("utf-8", errors="replace").rstrip("\r")
                    return None
                self._buf += chunk

    def close(self) -> None:
        if self._proc.stdin:
            self._proc.stdin.close()
        self._proc.terminate()
        try:
            self._proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            self._proc.kill()
            self._proc.wait()


@pytest.fixture(scope="session")
def server_exe() -> Path:
    """Locate the json_serial_server executable from the cmake build."""
    # The build directory is at the repo root
    repo = Path(__file__).parents[2]
    exe = repo / "build" / "test" / "json_serial_server"
    if not exe.exists():
        pytest.skip(f"server not built: {exe}")
    return exe


@pytest.fixture
def server(server_exe: Path) -> SubprocessTransport:
    """Launch a fresh server process."""
    transport = SubprocessTransport(server_exe)
    yield transport
    transport.close()


@pytest.fixture
def client(server: SubprocessTransport) -> MenuClient:
    """MenuClient connected to a server subprocess."""
    return MenuClient(transport=server, timeout=2.0)
