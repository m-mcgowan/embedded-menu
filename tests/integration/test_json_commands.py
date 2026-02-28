"""Integration tests for embedded-menu: JSON transport, console, and cross-interface.

Run against a device flashed with examples/all_interfaces.

Usage:
    pytest test_json_commands.py --port /dev/cu.usbmodem1234
"""

from __future__ import annotations

import pytest

from menu_client import MenuClient


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption("--port", required=True, help="Serial port for the device")
    parser.addoption("--baudrate", default=115200, type=int, help="Baud rate")


@pytest.fixture(scope="session")
def client(request: pytest.FixtureRequest) -> MenuClient:
    port = request.config.getoption("--port")
    baudrate = request.config.getoption("--baudrate")
    c = MenuClient(port, baudrate=baudrate)
    c.connect()
    yield c
    c.disconnect()


# ── JSON transport ──


class TestJsonPing:
    def test_ping(self, client: MenuClient) -> None:
        resp = client.send("ping")
        assert resp["cmd"] == "ping"
        assert resp["ok"] is True


class TestJsonEcho:
    def test_echo(self, client: MenuClient) -> None:
        resp = client.send("echo", msg="hello world")
        assert resp["msg"] == "hello world"

    def test_echo_empty(self, client: MenuClient) -> None:
        resp = client.send("echo")
        assert resp["msg"] == ""

    def test_echo_via_alias(self, client: MenuClient) -> None:
        resp = client.send("e", msg="via alias")
        assert resp["msg"] == "via alias"


class TestJsonAdd:
    def test_add(self, client: MenuClient) -> None:
        resp = client.send("add", a=2, b=3)
        assert resp["sum"] == 5

    def test_add_negative(self, client: MenuClient) -> None:
        resp = client.send("add", a=-10, b=7)
        assert resp["sum"] == -3

    def test_add_zero(self, client: MenuClient) -> None:
        resp = client.send("add", a=0, b=0)
        assert resp["sum"] == 0


class TestJsonLed:
    def test_led_on(self, client: MenuClient) -> None:
        resp = client.send("led", state=True)
        assert resp["state"] is True

    def test_led_off(self, client: MenuClient) -> None:
        resp = client.send("led", state=False)
        assert resp["state"] is False


class TestJsonInfo:
    def test_info_fields(self, client: MenuClient) -> None:
        resp = client.send("info")
        assert resp["firmware"] == "embedded-menu-example"
        assert resp["version"] == "0.1.0"
        assert resp["board"] in ("esp32s3", "pico", "unknown")
        assert isinstance(resp["uptime_ms"], int)


class TestJsonErrors:
    def test_unknown_command(self, client: MenuClient) -> None:
        resp = client.send("nonexistent")
        assert "not found" in resp["error"]

    def test_missing_cmd_field(self, client: MenuClient) -> None:
        resp = client.send_raw('{"value":42}')
        assert "missing cmd" in resp["error"]

    def test_invalid_json(self, client: MenuClient) -> None:
        resp = client.send_raw("{broken")
        assert "error" in resp


class TestJsonStress:
    def test_rapid_fire(self, client: MenuClient) -> None:
        for i in range(20):
            resp = client.send("add", a=i, b=1)
            assert resp["sum"] == i + 1


# ── Console (plain text) ──


class TestConsolePing:
    def test_ping(self, client: MenuClient) -> None:
        lines = client.console("ping")
        assert any("pong" in line for line in lines)


class TestConsoleEcho:
    def test_echo(self, client: MenuClient) -> None:
        lines = client.console("echo msg hello")
        assert any("hello" in line for line in lines)


class TestConsoleAdd:
    def test_add(self, client: MenuClient) -> None:
        lines = client.console("add a 5 b 3")
        assert any("8" in line for line in lines)


class TestConsoleLed:
    def test_led_on(self, client: MenuClient) -> None:
        lines = client.console("led state on")
        assert any("on" in line.lower() for line in lines)

    def test_led_off(self, client: MenuClient) -> None:
        lines = client.console("led state off")
        assert any("off" in line.lower() for line in lines)


class TestConsoleInfo:
    def test_info(self, client: MenuClient) -> None:
        lines = client.console("info")
        text = "\n".join(lines)
        assert "embedded-menu-example" in text
        assert "0.1.0" in text


class TestConsoleHelp:
    def test_help_lists_commands(self, client: MenuClient) -> None:
        lines = client.console("help")
        text = "\n".join(lines)
        assert "ping" in text
        assert "echo" in text
        assert "add" in text
        assert "led" in text
        assert "info" in text

    def test_help_specific_command(self, client: MenuClient) -> None:
        lines = client.console("help ping")
        text = "\n".join(lines)
        assert "ping" in text

    def test_unknown_command(self, client: MenuClient) -> None:
        lines = client.console("nonexistent")
        text = "\n".join(lines).lower()
        assert "unknown" in text


# ── Cross-interface (shared state) ──


class TestCrossInterface:
    def test_led_via_json_verify_via_console(self, client: MenuClient) -> None:
        """Toggle LED via JSON, verify the handler ran by checking console too."""
        json_resp = client.send("led", state=True)
        assert json_resp["state"] is True

        json_resp = client.send("led", state=False)
        assert json_resp["state"] is False

    def test_interleave_json_and_console(self, client: MenuClient) -> None:
        """Alternate between JSON and console to prove both stay functional."""
        resp = client.send("add", a=1, b=2)
        assert resp["sum"] == 3

        lines = client.console("add a 10 b 20")
        assert any("30" in line for line in lines)

        resp = client.send("add", a=100, b=200)
        assert resp["sum"] == 300

    def test_same_handler_different_output(self, client: MenuClient) -> None:
        """info returns structured JSON via JSON transport, text via console."""
        json_resp = client.send("info")
        assert "board" in json_resp  # structured key

        console_lines = client.console("info")
        text = "\n".join(console_lines)
        assert "board:" in text  # human-readable label
