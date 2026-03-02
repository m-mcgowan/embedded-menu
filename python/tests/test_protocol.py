"""Tests for protocol types and serialization."""

from __future__ import annotations

import json

from embedded_menu.protocol import (
    CommandInfo,
    MenuResponse,
    MenuError,
    build_request,
)


class TestBuildRequest:
    def test_no_params(self) -> None:
        line = build_request("ping")
        assert json.loads(line) == {"cmd": "ping"}

    def test_with_params(self) -> None:
        line = build_request("add", a=2, b=3)
        d = json.loads(line)
        assert d == {"cmd": "add", "a": 2, "b": 3}

    def test_bool_param(self) -> None:
        line = build_request("led", state=True)
        d = json.loads(line)
        assert d == {"cmd": "led", "state": True}

    def test_string_param(self) -> None:
        line = build_request("echo", msg="hello world")
        d = json.loads(line)
        assert d == {"cmd": "echo", "msg": "hello world"}

    def test_compact_format(self) -> None:
        line = build_request("ping")
        assert " " not in line  # no spaces in compact JSON


class TestMenuResponse:
    def test_from_json_line(self) -> None:
        resp = MenuResponse.from_json_line('{"cmd":"ping","ok":true}')
        assert resp.cmd == "ping"
        assert resp.ok is True
        assert resp["ok"] is True

    def test_error_response(self) -> None:
        resp = MenuResponse.from_json_line('{"cmd":"bad","error":"not found"}')
        assert resp.cmd == "bad"
        assert resp.ok is False
        assert resp.error == "not found"

    def test_error_without_cmd(self) -> None:
        resp = MenuResponse.from_json_line('{"error":"parse error"}')
        assert resp.cmd is None
        assert resp.ok is False
        assert resp.error == "parse error"

    def test_fields_access(self) -> None:
        resp = MenuResponse.from_json_line('{"cmd":"add","sum":5}')
        assert resp["sum"] == 5
        assert "sum" in resp

    def test_contains(self) -> None:
        resp = MenuResponse.from_json_line('{"cmd":"ping","ok":true}')
        assert "ok" in resp
        assert "missing" not in resp


class TestCommandInfo:
    def test_from_dict_full(self) -> None:
        d = {
            "name": "ping",
            "help": "Echo a ping",
            "group": "system",
            "aliases": ["p"],
        }
        info = CommandInfo.from_dict(d)
        assert info.name == "ping"
        assert info.help == "Echo a ping"
        assert info.group == "system"
        assert info.aliases == ["p"]

    def test_from_dict_minimal(self) -> None:
        d = {"name": "ping"}
        info = CommandInfo.from_dict(d)
        assert info.name == "ping"
        assert info.help is None
        assert info.group is None
        assert info.aliases == []

    def test_from_dict_null_fields(self) -> None:
        d = {"name": "ping", "help": None, "group": None, "aliases": []}
        info = CommandInfo.from_dict(d)
        assert info.help is None
        assert info.group is None


class TestMenuError:
    def test_error_message(self) -> None:
        resp = MenuResponse.from_json_line('{"cmd":"x","error":"not found"}')
        err = MenuError(resp)
        assert str(err) == "not found"
        assert err.response is resp
