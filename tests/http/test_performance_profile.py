"""Read-only discovery and rejection checks for the optional performance DLL."""

import pytest


@pytest.fixture
def performance_client(client):
    if client.ok("ping").get("profile") != "performance":
        pytest.skip("requires the performance profile")
    return client


def test_performance_catalog_is_restricted(performance_client):
    tools = {tool["name"]: tool for tool in performance_client.tools()}
    assert set(tools) == {
        "game", "inspect", "ping", "mcp_bridge_setup",
        "communityshaders.profiler_api", "communityshaders.performance_tuning",
        "communityshaders.screenshot",
    }
    assert tools["game"]["inputSchema"]["properties"]["action"]["enum"] == [
        "list", "load", "loadLast",
    ]


@pytest.mark.parametrize("tool,args", [
    ("game", {"action": "save", "name": "must-not-save"}),
    ("game", {"action": "load", "name": "missing", "scale": 2}),
    ("inspect", {"kind": "inventory"}),
    ("communityshaders.profiler_api", {
        "contractMajor": 1, "clientId": "test", "commandId": "invalid",
        "action": "start_capture", "frameCount": 301,
    }),
])
def test_out_of_scope_requests_fail_before_dispatch(performance_client, tool, args):
    status, _ = performance_client.call(tool, args)
    assert status == 400
