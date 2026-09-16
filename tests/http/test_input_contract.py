"""Offline input discovery regressions; never contact a game or inject input."""

from __future__ import annotations

import json
from pathlib import Path

import pytest
import requests

import test_input as input_checks


@pytest.fixture(autouse=True)
def no_http(monkeypatch):
    """Fail any accidental HTTP request before it can reach a running game."""
    def reject_request(*args, **kwargs):
        """Prevent live discovery from being introduced into an offline test."""
        pytest.fail("offline input contract test attempted HTTP")

    monkeypatch.setattr(requests.sessions.Session, "request", reject_request)


@pytest.fixture
def fallback_schema():
    """Load the schema actually bundled by the bridge for offline discovery."""
    path = Path(__file__).resolve().parents[2] / "bridge/src/tools-fallback.json"
    return {tool["name"]: tool for tool in json.loads(path.read_text(encoding="utf-8"))["tools"]}


class CapabilityClient:
    """Serve a v2 capability response with no game dependency."""

    def __init__(self):
        """Provide the required keyboard and tracked-set capability fields."""
        self.body = {
            "contract": {"name": "devbench.input", "version": {"major": 2, "minor": 0}},
            "capabilities": {
                "keyboard": {
                    "available": True,
                    "encoding": "DirectInputScanCode",
                    "injection": "Skyrim.BSInputEventQueue",
                    "actions": ["status", "down", "up", "tap", "sequence", "releaseAll"],
                    "keys": [{"key": "enter", "scancode": 0x1C}],
                },
                "vrTrackedSet": {
                    "atomicDevices": ["hmd", "left", "right"],
                    "passThroughWhenInactive": True,
                    "actions": ["status", "observe", "sequence", "stop", "releaseAll"],
                },
            },
        }

    def ok(self, tool, args):
        """Only capability discovery is allowed through this test client."""
        assert (tool, args) == ("input", {"action": "capabilities"})
        return self.body


def test_bridge_fallback_accepts_complete_input_contract(fallback_schema):
    """The bundled fallback can discover every required input capability."""
    input_checks.test_input_capability_contract(CapabilityClient(), fallback_schema)


@pytest.mark.parametrize(
    "action",
    ["capabilities", "status", "observe", "down", "up", "tap", "sequence", "stop", "releaseAll"],
)
def test_missing_schema_action_fails_instead_of_skipping(fallback_schema, action):
    """An omitted advertised action is a regression, including capabilities."""
    fallback_schema["input"]["inputSchema"]["properties"]["action"]["enum"].remove(action)
    with pytest.raises(AssertionError):
        input_checks.test_input_capability_contract(CapabilityClient(), fallback_schema)


def test_missing_action_enum_fails(fallback_schema):
    """Removing discovery constraints must not silently disable coverage."""
    del fallback_schema["input"]["inputSchema"]["properties"]["action"]["enum"]
    with pytest.raises(AssertionError):
        input_checks.test_input_capability_contract(CapabilityClient(), fallback_schema)


@pytest.mark.parametrize("action", ["status", "observe", "sequence", "stop", "releaseAll"])
def test_missing_vr_capability_action_fails(fallback_schema, action):
    """A complete schema cannot hide a missing v2 tracked-set capability."""
    client = CapabilityClient()
    client.body["capabilities"]["vrTrackedSet"]["actions"].remove(action)
    with pytest.raises(AssertionError):
        input_checks.test_input_capability_contract(client, fallback_schema)


def test_bridge_fallback_advertises_stop_guard(fallback_schema):
    """Offline clients must discover the guarded-stop argument and its bounds."""
    guard = fallback_schema["record"]["inputSchema"]["properties"]["expectedCorrelationId"]
    assert guard["type"] == "string"
    assert guard["minLength"] == 1
    assert guard["maxLength"] == 128
    assert "expectedCorrelationId" not in fallback_schema["record"]["inputSchema"].get("required", [])
