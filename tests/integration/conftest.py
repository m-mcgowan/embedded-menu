"""Shared pytest configuration for integration tests."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

# Allow importing menu_client / firmware_sim from this directory
sys.path.insert(0, str(Path(__file__).parent))


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption(
        "--port",
        default=None,
        help="Serial port for hardware tests. Omit to run against the PTY simulator.",
    )
    parser.addoption("--baudrate", default=115200, type=int)
