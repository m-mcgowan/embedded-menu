#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

# Find a venv with pytest
if [ -d .venv ]; then
    PYTHON=.venv/bin/python
elif [ -d ../tests/integration/.venv ]; then
    PYTHON=../tests/integration/.venv/bin/python
else
    PYTHON=python3
fi

PYTHONPATH=src "$PYTHON" -m pytest tests/ "$@"
