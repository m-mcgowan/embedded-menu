#!/usr/bin/env python3
"""Serial ↔ WebSocket bridge for embedded-menu devices.

Opens a serial port, starts a WebSocket server, and relays JSON lines
bidirectionally. Optionally opens the SPA in the default browser.

Handles device disconnection (e.g. deep sleep, USB reset) by automatically
reconnecting when the serial port reappears.

Usage:
    python bridge.py --port /dev/cu.usbmodem1433101
    python bridge.py --port /dev/cu.usbmodem1433101 --ws-port 9000 --no-browser
"""

from __future__ import annotations

import argparse
import asyncio
import logging
import webbrowser
from pathlib import Path

import serial
import websockets
from websockets.asyncio.server import serve

log = logging.getLogger("bridge")


class SerialBridge:
    def __init__(self, port: str, baudrate: int = 115200):
        self.port = port
        self.baudrate = baudrate
        self.serial: serial.Serial | None = None
        self.clients: set = set()

    def _open_serial(self) -> bool:
        """Try to open the serial port. Returns True on success."""
        try:
            self.serial = serial.Serial(self.port, self.baudrate, timeout=0.05)
            log.info("Serial port opened: %s", self.port)
            return True
        except serial.SerialException as e:
            self.serial = None
            log.debug("Serial port unavailable: %s", e)
            return False

    async def serial_reader(self) -> None:
        """Read lines from serial and broadcast to all WebSocket clients.

        Handles device disconnection by waiting for the port to reappear.
        """
        loop = asyncio.get_event_loop()

        while True:
            if self.serial is None:
                if self._open_serial():
                    self._broadcast_status("connected")
                else:
                    await asyncio.sleep(1)
                    continue

            try:
                line = await loop.run_in_executor(None, self.serial.readline)
            except (serial.SerialException, OSError) as e:
                log.warning("Serial read error (device disconnected?): %s", e)
                self._close_serial()
                self._broadcast_status("disconnected")
                continue

            if line:
                text = line.decode("utf-8", errors="replace").strip()
                if text:
                    log.debug("serial → ws: %s", text)
                    await self._broadcast(text)
            else:
                await asyncio.sleep(0.01)

    async def ws_handler(self, ws) -> None:
        """Handle a WebSocket client connection."""
        self.clients.add(ws)
        peer = ws.remote_address
        log.info("client connected: %s", peer)
        try:
            async for message in ws:
                text = message.strip() if isinstance(message, str) else message.decode().strip()
                log.debug("ws → serial: %s", text)
                if self.serial and self.serial.is_open:
                    try:
                        self.serial.write((text + "\n").encode())
                        self.serial.flush()
                    except (serial.SerialException, OSError) as e:
                        log.warning("Serial write error: %s", e)
                        self._close_serial()
                        self._broadcast_status("disconnected")
                else:
                    log.warning("Serial port not available, dropping message")
        except websockets.ConnectionClosed:
            pass
        finally:
            self.clients.discard(ws)
            log.info("client disconnected: %s", peer)

    async def _broadcast(self, text: str) -> None:
        dead = set()
        for ws in self.clients:
            try:
                await ws.send(text)
            except websockets.ConnectionClosed:
                dead.add(ws)
        self.clients -= dead

    def _broadcast_status(self, state: str) -> None:
        """Log serial connection state changes."""
        log.info("Serial %s: %s", state, self.port)

    def _close_serial(self) -> None:
        if self.serial:
            try:
                self.serial.close()
            except Exception:
                pass
            self.serial = None

    def close(self) -> None:
        self._close_serial()


async def main() -> None:
    parser = argparse.ArgumentParser(description="Serial ↔ WebSocket bridge")
    parser.add_argument("--port", required=True, help="Serial port")
    parser.add_argument("--baudrate", type=int, default=115200)
    parser.add_argument("--ws-port", type=int, default=0, help="WebSocket server port (0 = auto)")
    parser.add_argument(
        "--spa",
        default="index.html",
        help="SPA filename to open (default: index.html)",
    )
    parser.add_argument("--no-browser", action="store_true", help="Don't open browser")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(name)s %(levelname)s %(message)s",
    )

    bridge = SerialBridge(args.port, args.baudrate)

    server = await serve(bridge.ws_handler, "localhost", args.ws_port)
    actual_port = server.sockets[0].getsockname()[1]

    log.info("WebSocket server on ws://localhost:%d", actual_port)
    log.info("Serial port: %s @ %d", args.port, args.baudrate)

    if not args.no_browser:
        spa_path = Path(__file__).parent / args.spa
        url = f"file://{spa_path.resolve()}?ws=ws://localhost:{actual_port}"
        log.info("Opening browser: %s", url)
        webbrowser.open(url)

    try:
        await bridge.serial_reader()
    finally:
        server.close()
        await server.wait_closed()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        log.info("Shutting down")
