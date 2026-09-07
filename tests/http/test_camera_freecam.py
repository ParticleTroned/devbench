"""Opt-in VR free-camera regression test against a running, repaired DevBench.

Set DEVBENCH_TEST_FREECAM=1 and DEVBENCH_URL to the intended test instance.
The default skip happens before server discovery or player bootstrap. Even when
enabled, the backend marker is checked before any camera mutation, so an older
build cannot accidentally enter Skyrim VR's crashing native toggle.

Run in a stationary, unobstructed scene without movement input. This checks the
camera-node transform after subsequent game frames; visually inspect both eyes
separately to qualify stereo presentation (docs/vr-free-camera.md).
"""

from __future__ import annotations

import math
import os
import time

import pytest

from conftest import require_enum, require_tool


pytestmark = pytest.mark.skipif(
    os.environ.get("DEVBENCH_TEST_FREECAM") != "1",
    reason="set DEVBENCH_TEST_FREECAM=1 to run the VR free-camera round trip",
)


@pytest.fixture
def vr_camera_session(client, tool_schema, request):
    camera = require_tool(tool_schema, "camera")
    for action in ("get", "freecam", "drive"):
        require_enum(camera, "action", action)
    inspect = require_tool(tool_schema, "inspect")
    for kind in ("health", "scene"):
        require_enum(inspect, "kind", kind)

    initial = client.ok("camera", {"action": "get"})
    if initial.get("freeCamBackend") != "vr-state":
        pytest.skip("this instance does not advertise the repaired vr-state backend")

    # Deliberately resolve this only after the safe backend read. Unlike the
    # requires_player marker, it cannot bootstrap an unrecognized build first.
    request.getfixturevalue("requires_player")
    initial = client.ok("camera", {"action": "get"})
    assert initial.get("freeCamBackend") == "vr-state", initial
    if initial.get("freeCam") is True:
        pytest.skip("free camera is already active; preserve its existing owner/view")
    assert initial.get("freeCam") is False, initial
    assert type(initial.get("stateId")) is int, initial
    assert initial["stateId"] != 3, initial
    health = client.ok("inspect", {"kind": "health"})
    assert health.get("vr") is True, health
    return initial, health["pid"]


def _after_frames(client, pid, count=3):
    start = client.ok("inspect", {"kind": "health"})
    assert start["pid"] == pid, start
    deadline = time.monotonic() + 8.0
    while time.monotonic() < deadline:
        health = client.ok("inspect", {"kind": "health"})
        assert health["pid"] == pid, "game instance changed during camera test"
        if health["frame"] >= start["frame"] + count:
            return client.ok("camera", {"action": "get"})
        time.sleep(0.05)
    pytest.fail(f"game frames stopped advancing after {start['frame']}")


def _set_freecam(client, on):
    result = client.ok("camera", {"action": "freecam", "on": on})
    assert result.get("queued") is False, result
    assert result.get("action") == "freecam", result
    assert result.get("on") is on, result
    assert result.get("freeCam") is on, result


def _drive(client, position, pitch, yaw):
    result = client.ok("camera", {
        "action": "drive",
        "x": position[0], "y": position[1], "z": position[2],
        "pitch": pitch, "yaw": yaw,
    })
    assert result.get("queued") is False, result
    assert result.get("action") == "drive", result


def _assert_position(camera, expected):
    actual = [camera[key] for key in ("camX", "camY", "camZ")]
    assert actual == pytest.approx(expected, abs=0.25, rel=0), camera


def _angles(camera):
    angles = [camera[key] for key in ("camPitch", "camYaw")]
    assert all(math.isfinite(value) for value in angles), camera
    return angles


def _angle_distance(a, b):
    return abs(math.atan2(math.sin(a - b), math.cos(a - b)))


def test_vr_drive_requires_free_camera(client, vr_camera_session):
    initial, _ = vr_camera_session
    status, result = client.call("camera", {
        "action": "drive", "x": 100.0, "y": 50.0, "z": 25.0,
        "pitch": 0.15, "yaw": 0.25,
    })
    assert status == 409, (status, result)
    current = client.ok("camera", {"action": "get"})
    assert current["stateId"] == initial["stateId"], current
    assert current["freeCam"] is False, current


def test_vr_free_camera_drive_persists_and_restores(client, vr_camera_session):
    initial, pid = vr_camera_session
    scene = client.ok("inspect", {"kind": "scene"})
    assert scene.get("playerLoaded") is True, scene
    player_position = scene["position"]
    initial_position = [initial[key] for key in ("camX", "camY", "camZ")]

    try:
        for cycle in range(3):
            _set_freecam(client, True)
            _set_freecam(client, True)  # Must not replace the retained return state.
            active = _after_frames(client, pid)
            assert active["freeCam"] is True and active["stateId"] == 3, active

            # Establish a known orientation before testing combined pitch/yaw;
            # world Euler readback need not equal native free-camera angles.
            _drive(client, initial_position, pitch=0.0, yaw=0.0)
            baseline = _after_frames(client, pid)
            _assert_position(baseline, initial_position)
            baseline_angles = _angles(baseline)

            target = [
                initial_position[0] + 75.0 + cycle * 10.0,
                initial_position[1] + 25.0,
                initial_position[2] + 15.0,
            ]
            _drive(client, target, pitch=0.15, yaw=0.25)
            driven = _after_frames(client, pid)
            assert driven["freeCam"] is True and driven["stateId"] == 3, driven
            _assert_position(driven, target)
            driven_angles = _angles(driven)
            for before, after in zip(baseline_angles, driven_angles):
                assert _angle_distance(before, after) > 0.05, (baseline, driven)

            held = _after_frames(client, pid, count=12)
            _assert_position(held, target)
            for before, after in zip(driven_angles, _angles(held)):
                assert _angle_distance(before, after) < 0.01, (driven, held)
            current_scene = client.ok("inspect", {"kind": "scene"})
            assert current_scene["position"] == pytest.approx(
                player_position, abs=0.25, rel=0,
            ), current_scene

            _set_freecam(client, False)
            _set_freecam(client, False)
            restored = _after_frames(client, pid)
            assert restored["freeCam"] is False, restored
            assert restored["stateId"] == initial["stateId"], (initial, restored)
    finally:
        # The backend and initial inactive state were validated before entering
        # this block. Cleanup therefore cannot toggle an old native VR build.
        client.ok("camera", {"action": "freecam", "on": False})
