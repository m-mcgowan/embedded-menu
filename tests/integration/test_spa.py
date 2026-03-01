"""Browser tests for the embedded-menu SPA.

Uses Playwright to drive the SPA against a mock WebSocket server,
so no real device is needed.
"""

from __future__ import annotations

import asyncio
import json
import threading
import time
from pathlib import Path

import pytest
from playwright.sync_api import sync_playwright, Page, expect

import websockets
from websockets.asyncio.server import serve

SPA_PATH = Path(__file__).resolve().parent.parent.parent / "web" / "index.html"
WS_PORT = 8799  # avoid colliding with real bridge


class MockDevice:
    """Mock WebSocket server that echoes commands like the example firmware."""

    def __init__(self, port: int = WS_PORT):
        self.port = port
        self._loop: asyncio.AbstractEventLoop | None = None
        self._thread: threading.Thread | None = None
        self._received: list[str] = []

    @property
    def received(self) -> list[str]:
        return list(self._received)

    async def _handler(self, ws):
        async for message in ws:
            text = message.strip() if isinstance(message, str) else message.decode().strip()
            self._received.append(text)
            try:
                req = json.loads(text)
                if req.get("cmd") == "sleep":
                    ms = req.get("ms", 1000)
                    await ws.send(json.dumps({"cmd": "sleep", "sleeping": ms}))
                    await ws.close()
                    return
                resp = self._handle_command(req)
                await ws.send(json.dumps(resp))
            except json.JSONDecodeError:
                await ws.send(text)

    def _handle_command(self, req: dict) -> dict:
        cmd = req.get("cmd", "")
        if cmd == "ping":
            return {"cmd": "ping", "ok": True}
        elif cmd == "add":
            return {"cmd": "add", "sum": req.get("a", 0) + req.get("b", 0)}
        elif cmd == "echo":
            return {"cmd": "echo", "msg": req.get("msg", "")}
        elif cmd == "info":
            return {
                "cmd": "info",
                "board": "mock",
                "firmware": "test",
                "version": "0.0.1",
            }
        else:
            return {"cmd": cmd, "error": "not found"}

    async def _run(self):
        self._server = await serve(self._handler, "localhost", self.port)
        self._started.set()
        await self._stop_event.wait()
        self._server.close()
        await self._server.wait_closed()

    def start(self):
        self._loop = asyncio.new_event_loop()
        self._stop_event = asyncio.Event()
        self._started = threading.Event()
        self._thread = threading.Thread(target=self._loop.run_until_complete, args=(self._run(),), daemon=True)
        self._thread.start()
        self._started.wait(timeout=5)

    def stop(self):
        if self._loop and self._stop_event:
            self._loop.call_soon_threadsafe(self._stop_event.set)
        if self._thread:
            self._thread.join(timeout=5)


@pytest.fixture(scope="module")
def mock_device():
    device = MockDevice()
    device.start()
    yield device
    device.stop()


@pytest.fixture(scope="module")
def browser_context(mock_device):
    with sync_playwright() as p:
        browser = p.chromium.launch()
        context = browser.new_context()
        yield context
        context.close()
        browser.close()


@pytest.fixture()
def page(browser_context, mock_device) -> Page:
    pg = browser_context.new_page()
    url = f"file://{SPA_PATH}?ws=ws://localhost:{WS_PORT}"
    pg.goto(url)
    # Wait for WebSocket connection
    pg.wait_for_selector("#status.connected", timeout=5000)
    yield pg
    pg.close()


class TestConnection:
    def test_shows_connected_status(self, page: Page):
        status = page.locator("#status")
        expect(status).to_have_text("connected")
        expect(status).to_have_class("connected")

    def test_shows_connected_info_line(self, page: Page):
        terminal = page.locator("#terminal")
        expect(terminal).to_contain_text("Connected to")


class TestTerminal:
    def test_send_ping(self, page: Page):
        page.fill("#cmd-input", '{"cmd":"ping"}')
        page.click("#send-btn")
        terminal = page.locator("#terminal")
        # Sent line
        expect(terminal).to_contain_text('{"cmd":"ping"}')
        # Response
        page.wait_for_selector(".line.received", timeout=3000)
        expect(terminal).to_contain_text('"ok": true')

    def test_send_via_enter_key(self, page: Page):
        page.fill("#cmd-input", '{"cmd":"echo","msg":"hi"}')
        page.press("#cmd-input", "Enter")
        terminal = page.locator("#terminal")
        page.wait_for_selector(".line.received", timeout=3000)
        expect(terminal).to_contain_text('"msg":"hi"')

    def test_input_cleared_after_send(self, page: Page):
        page.fill("#cmd-input", '{"cmd":"ping"}')
        page.click("#send-btn")
        expect(page.locator("#cmd-input")).to_have_value("")

    def test_command_history_up_down(self, page: Page):
        # Send two commands
        page.fill("#cmd-input", "first")
        page.press("#cmd-input", "Enter")
        page.fill("#cmd-input", "second")
        page.press("#cmd-input", "Enter")

        # Arrow up twice
        page.press("#cmd-input", "ArrowUp")
        expect(page.locator("#cmd-input")).to_have_value("second")
        page.press("#cmd-input", "ArrowUp")
        expect(page.locator("#cmd-input")).to_have_value("first")

        # Arrow down
        page.press("#cmd-input", "ArrowDown")
        expect(page.locator("#cmd-input")).to_have_value("second")

        # Arrow down past end clears
        page.press("#cmd-input", "ArrowDown")
        expect(page.locator("#cmd-input")).to_have_value("")

    def test_sent_and_received_line_classes(self, page: Page):
        page.fill("#cmd-input", '{"cmd":"ping"}')
        page.click("#send-btn")
        page.wait_for_selector(".line.received", timeout=3000)

        sent = page.locator(".line.sent").last
        expect(sent).to_contain_text("ping")

        received = page.locator(".line.received").last
        expect(received).to_contain_text("ok")


class TestPlugins:
    def test_device_info_panel_renders(self, page: Page):
        page.fill("#cmd-input", '{"cmd":"info"}')
        page.click("#send-btn")
        page.wait_for_selector(".line.received", timeout=3000)

        # Panel sidebar should be visible
        panels = page.locator("#panels")
        expect(panels).to_have_class("has-panels")

        # Device Info panel should have rendered key-value pairs
        panel_body = page.locator(".panel-body").first
        expect(panel_body).to_contain_text("board")
        expect(panel_body).to_contain_text("mock")
        expect(panel_body).to_contain_text("firmware")
        expect(panel_body).to_contain_text("test")

    def test_panel_updates_on_new_data(self, page: Page):
        page.fill("#cmd-input", '{"cmd":"info"}')
        page.click("#send-btn")
        page.wait_for_selector(".line.received", timeout=3000)

        panel_body = page.locator(".panel-body").first
        expect(panel_body).to_contain_text("0.0.1")


class TestAddCommand:
    def test_add_returns_sum(self, page: Page):
        page.fill("#cmd-input", '{"cmd":"add","a":10,"b":20}')
        page.click("#send-btn")
        page.wait_for_selector(".line.received", timeout=3000)
        terminal = page.locator("#terminal")
        expect(terminal).to_contain_text('"sum": 30')


class TestErrorHandling:
    def test_unknown_command(self, page: Page):
        page.fill("#cmd-input", '{"cmd":"nonexistent"}')
        page.click("#send-btn")
        page.wait_for_selector(".line.received", timeout=3000)
        terminal = page.locator("#terminal")
        expect(terminal).to_contain_text("not found")


class TestReconnection:
    def test_reconnects_after_server_closes_connection(self, page: Page):
        # Verify we start connected
        expect(page.locator("#status")).to_have_text("connected")

        # Send sleep command — mock server will close the connection
        page.fill("#cmd-input", '{"cmd":"sleep","ms":100}')
        page.click("#send-btn")

        # Should transition to disconnected
        page.wait_for_selector("#status.disconnected", timeout=5000)
        expect(page.locator("#status")).to_have_text("disconnected")

        # SPA auto-reconnects after 2s — wait for it
        page.wait_for_selector("#status.connected", timeout=10000)
        expect(page.locator("#status")).to_have_text("connected")

        # Verify we can send commands again after reconnect
        page.fill("#cmd-input", '{"cmd":"ping"}')
        page.click("#send-btn")
        page.wait_for_selector(".line.received >> nth=-1", timeout=3000)
        terminal = page.locator("#terminal")
        expect(terminal).to_contain_text('"ok": true')
