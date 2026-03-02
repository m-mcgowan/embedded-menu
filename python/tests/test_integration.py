"""Cross-language integration tests.

These tests launch a C++ json_serial_server subprocess and drive it
from Python via MenuClient, proving the JSON protocol works end-to-end.
"""

from __future__ import annotations

import json

import pytest

from embedded_menu.client import MenuClient
from embedded_menu.protocol import MenuError

from conftest import SubprocessTransport


def _read_json_line(transport: SubprocessTransport, timeout: float = 2.0) -> str | None:
    """Read lines from transport until one contains JSON, return the JSON part."""
    while True:
        line = transport.read_line(timeout=timeout)
        if line is None:
            return None
        pos = line.find("{")
        if pos >= 0:
            return line[pos:]


class TestTUI:
    def test_receives_tui_banner(self, server: SubprocessTransport) -> None:
        """The server emits unsolicited TUI output on startup."""
        line1 = server.read_line(timeout=2.0)
        assert line1 is not None
        assert "Menu" in line1

    def test_disable_tui(self, server: SubprocessTransport) -> None:
        """Disabling TUI stops prompt output between commands."""
        # Drain the banner
        while True:
            line = server.read_line(timeout=0.5)
            if line is None:
                break

        # Disable TUI
        server.write_line('{"cmd":"tui","enabled":false}')
        resp_line = _read_json_line(server)
        assert resp_line is not None
        resp = json.loads(resp_line)
        assert resp["cmd"] == "tui"
        assert resp["enabled"] is False

        # Send another command — should get clean JSON, no prompt prefix
        server.write_line('{"cmd":"ping"}')
        resp_line = _read_json_line(server)
        assert resp_line is not None
        resp = json.loads(resp_line)
        assert resp["cmd"] == "ping"
        assert resp["ok"] is True

        # Verify the response line starts with JSON (no "> " prefix)
        assert resp_line.startswith("{")

    def test_tui_false_lifecycle(self, server_exe) -> None:
        """MenuClient(tui=False) disables on init, re-enables on close."""
        transport = SubprocessTransport(server_exe)
        try:
            # Drain banner
            while transport.read_line(timeout=0.5):
                pass

            with MenuClient(transport=transport, tui=False, timeout=2.0) as client:
                # TUI is disabled — commands should work cleanly
                resp = client.send("ping")
                assert resp["ok"] is True

            # After context manager exit, TUI should be re-enabled.
            # Query current TUI state directly to confirm.
            transport.write_line('{"cmd":"tui"}')
            resp_line = _read_json_line(transport)
            assert resp_line is not None
            resp = json.loads(resp_line)
            assert resp["cmd"] == "tui"
            assert resp["enabled"] is True
        finally:
            transport.close()


class TestCommands:
    def test_ping(self, client: MenuClient) -> None:
        resp = client.send("ping")
        assert resp["ok"] is True

    def test_echo_with_params(self, client: MenuClient) -> None:
        resp = client.send("echo", value="hello")
        assert resp["value"] == "hello"

    def test_add(self, client: MenuClient) -> None:
        resp = client.send("add", a=2, b=3)
        assert resp["sum"] == 5

    def test_unknown_command_error(self, client: MenuClient) -> None:
        with pytest.raises(MenuError) as exc_info:
            client.send("nonexistent")
        assert "not found" in str(exc_info.value)


class TestDiscovery:
    def test_help_lists_commands(self, client: MenuClient) -> None:
        commands = client.discover()
        names = [c.name for c in commands]
        assert "ping" in names
        assert "echo" in names
        assert "add" in names

    def test_help_has_descriptions(self, client: MenuClient) -> None:
        commands = client.discover()
        ping = next(c for c in commands if c.name == "ping")
        assert ping.help == "Echo a ping"

    def test_help_has_groups(self, client: MenuClient) -> None:
        commands = client.discover()
        add = next(c for c in commands if c.name == "add")
        assert add.group == "math"

    def test_help_topic(self, client: MenuClient) -> None:
        info = client.get_command_info("ping")
        assert info.name == "ping"
        assert info.help == "Echo a ping"
