"""Shared pytest configuration for integration tests."""

from __future__ import annotations

import sys
from pathlib import Path

# Allow importing menu_client from this directory
sys.path.insert(0, str(Path(__file__).parent))
