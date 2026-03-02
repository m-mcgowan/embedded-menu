"""Menu protocol types and serialization."""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from typing import Any


@dataclass(frozen=True)
class CommandInfo:
    """Metadata for a registered command, as returned by help discovery."""

    name: str
    help: str | None = None
    group: str | None = None
    aliases: list[str] = field(default_factory=list)

    @classmethod
    def from_dict(cls, d: dict) -> CommandInfo:
        return cls(
            name=d["name"],
            help=d.get("help"),
            group=d.get("group"),
            aliases=d.get("aliases", []),
        )


@dataclass
class MenuResponse:
    """Parsed response from a menu command."""

    cmd: str | None
    fields: dict[str, Any]
    raw: str

    @property
    def ok(self) -> bool:
        return "error" not in self.fields

    @property
    def error(self) -> str | None:
        return self.fields.get("error")

    def __getitem__(self, key: str) -> Any:
        return self.fields[key]

    def __contains__(self, key: str) -> bool:
        return key in self.fields

    @classmethod
    def from_json_line(cls, line: str) -> MenuResponse:
        """Parse a JSON response line from the device."""
        d = json.loads(line)
        cmd = d.pop("cmd", None)
        return cls(cmd=cmd, fields=d, raw=line)


class MenuError(Exception):
    """Raised when a menu command returns an error."""

    def __init__(self, response: MenuResponse) -> None:
        self.response = response
        super().__init__(response.error or "unknown error")


def build_request(cmd: str, **params: Any) -> str:
    """Build a JSON request line for a menu command."""
    request = {"cmd": cmd, **params}
    return json.dumps(request, separators=(",", ":"))
