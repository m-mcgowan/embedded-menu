"""Tests for MenuClient with mock transport."""

from __future__ import annotations

import json
from collections import deque
from typing import Any

import pytest

from embedded_menu.client import MenuClient
from embedded_menu.protocol import MenuError


class MockTransport:
    """Mock line transport for testing MenuClient without a device."""

    def __init__(self) -> None:
        self.sent: list[str] = []
        self._responses: deque[str | None] = deque()

    def queue_response(self, line: str) -> None:
        """Queue a JSON response line to be returned by read_line."""
        self._responses.append(line)

    def queue_responses(self, *lines: str) -> None:
        for line in lines:
            self._responses.append(line)

    def write_line(self, line: str) -> None:
        self.sent.append(line)

    def read_line(self, timeout: float) -> str | None:
        if self._responses:
            return self._responses.popleft()
        return None


class TestMenuClientSend:
    def test_send_ping(self) -> None:
        transport = MockTransport()
        transport.queue_response('{"cmd":"ping","ok":true}')

        client = MenuClient(transport=transport)
        resp = client.send("ping")

        assert resp.cmd == "ping"
        assert resp["ok"] is True
        sent = json.loads(transport.sent[0])
        assert sent == {"cmd": "ping"}

    def test_send_with_params(self) -> None:
        transport = MockTransport()
        transport.queue_response('{"cmd":"add","sum":5}')

        client = MenuClient(transport=transport)
        resp = client.send("add", a=2, b=3)

        assert resp["sum"] == 5
        sent = json.loads(transport.sent[0])
        assert sent["a"] == 2
        assert sent["b"] == 3

    def test_send_error_raises(self) -> None:
        transport = MockTransport()
        transport.queue_response('{"cmd":"bad","error":"not found"}')

        client = MenuClient(transport=transport)
        with pytest.raises(MenuError) as exc_info:
            client.send("bad")

        assert "not found" in str(exc_info.value)
        assert exc_info.value.response.cmd == "bad"

    def test_send_skips_non_json_lines(self) -> None:
        transport = MockTransport()
        transport.queue_responses(
            "some debug output",
            "another line",
            '{"cmd":"ping","ok":true}',
        )

        client = MenuClient(transport=transport)
        resp = client.send("ping")
        assert resp["ok"] is True

    def test_send_timeout(self) -> None:
        transport = MockTransport()
        # No response queued

        client = MenuClient(transport=transport, timeout=0.1)
        with pytest.raises(TimeoutError):
            client.send("ping")


class TestMenuClientDiscover:
    def test_discover(self) -> None:
        transport = MockTransport()
        transport.queue_response(json.dumps({
            "cmd": "help",
            "commands": [
                {"name": "ping", "help": "Echo a ping", "group": None, "aliases": []},
                {"name": "led", "help": "LED ctrl", "group": "hw", "aliases": ["l"]},
            ],
        }))

        client = MenuClient(transport=transport)
        commands = client.discover()

        assert len(commands) == 2
        assert commands[0].name == "ping"
        assert commands[0].help == "Echo a ping"
        assert commands[0].group is None
        assert commands[1].name == "led"
        assert commands[1].group == "hw"
        assert commands[1].aliases == ["l"]

    def test_discover_empty(self) -> None:
        transport = MockTransport()
        transport.queue_response('{"cmd":"help","commands":[]}')

        client = MenuClient(transport=transport)
        commands = client.discover()
        assert commands == []

    def test_get_command_info(self) -> None:
        transport = MockTransport()
        transport.queue_response(json.dumps({
            "cmd": "help",
            "name": "ping",
            "help": "Echo a ping",
            "group": None,
            "aliases": [],
        }))

        client = MenuClient(transport=transport)
        info = client.get_command_info("ping")
        assert info.name == "ping"
        assert info.help == "Echo a ping"


class TestMenuClientConsole:
    def test_console(self) -> None:
        transport = MockTransport()
        transport.queue_responses(
            "  ping                 Echo a ping",
            "  echo                 Echo back",
            None,  # silence
        )

        client = MenuClient(transport=transport, timeout=0.1)
        lines = client.console("help")

        assert len(lines) == 2
        assert "ping" in lines[0]
        assert transport.sent[0] == "help"

    def test_console_filters_prompt(self) -> None:
        transport = MockTransport()
        transport.queue_responses(
            "pong",
            ">",  # prompt — should be filtered
            None,
        )

        client = MenuClient(transport=transport, timeout=0.1)
        lines = client.console("ping")
        assert lines == ["pong"]


class TestMenuClientLifecycle:
    def test_context_manager(self) -> None:
        transport = MockTransport()
        transport.queue_response('{"cmd":"ping","ok":true}')

        with MenuClient(transport=transport) as client:
            resp = client.send("ping")
            assert resp["ok"] is True

    def test_requires_port_or_transport(self) -> None:
        with pytest.raises(ValueError, match="either port or transport"):
            MenuClient()

    def test_send_raw(self) -> None:
        transport = MockTransport()
        transport.queue_response('{"error":"parse error"}')

        client = MenuClient(transport=transport)
        resp = client.send_raw("{broken")
        assert resp.error == "parse error"
        assert transport.sent[0] == "{broken"


class TestMenuClientTUI:
    def test_disable_tui(self) -> None:
        transport = MockTransport()
        transport.queue_response('{"cmd":"tui","enabled":false}')

        client = MenuClient(transport=transport)
        resp = client.disable_tui()

        assert resp["enabled"] is False
        sent = json.loads(transport.sent[0])
        assert sent == {"cmd": "tui", "enabled": False}

    def test_enable_tui(self) -> None:
        transport = MockTransport()
        transport.queue_response('{"cmd":"tui","enabled":true}')

        client = MenuClient(transport=transport)
        resp = client.enable_tui()

        assert resp["enabled"] is True
        sent = json.loads(transport.sent[0])
        assert sent == {"cmd": "tui", "enabled": True}

    def test_tui_false_disables_on_init(self) -> None:
        transport = MockTransport()
        transport.queue_response('{"cmd":"tui","enabled":false}')

        client = MenuClient(transport=transport, tui=False)

        # Should have sent tui disable automatically
        assert len(transport.sent) == 1
        sent = json.loads(transport.sent[0])
        assert sent == {"cmd": "tui", "enabled": False}
        assert client._tui_was_disabled is True

    def test_close_re_enables_tui(self) -> None:
        transport = MockTransport()
        # Response for disable on init
        transport.queue_response('{"cmd":"tui","enabled":false}')
        # Response for re-enable on close
        transport.queue_response('{"cmd":"tui","enabled":true}')

        client = MenuClient(transport=transport, tui=False)
        client.close()

        assert len(transport.sent) == 2
        sent = json.loads(transport.sent[1])
        assert sent == {"cmd": "tui", "enabled": True}

    def test_context_manager_re_enables_tui(self) -> None:
        transport = MockTransport()
        transport.queue_response('{"cmd":"tui","enabled":false}')
        transport.queue_response('{"cmd":"tui","enabled":true}')

        with MenuClient(transport=transport, tui=False) as client:
            pass

        assert len(transport.sent) == 2
        re_enable = json.loads(transport.sent[1])
        assert re_enable["enabled"] is True

    def test_tui_default_does_not_send(self) -> None:
        transport = MockTransport()
        client = MenuClient(transport=transport)

        # No messages should be sent on init with default tui=True
        assert len(transport.sent) == 0
        assert client._tui_was_disabled is False
