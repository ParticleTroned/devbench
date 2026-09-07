# Skyrim VR free camera

DevBench's VR camera path must enter the existing engine `FreeCameraState` directly. Calling `PlayerCamera::ToggleFreeCameraMode` in Skyrim VR 1.4.15 crashes while entering free camera, even though the engine constructs a usable free-camera state.

## Evidence from running-game memory

These findings come from preserved, decrypted **live mapped-memory images**, captured from running Skyrim VR processes on 2026-08-22 and 2026-08-25. The packed executable on disk was not the analysis input. Addresses below are RVAs relative to `SkyrimVR.exe`; raw images are not included in this repository.

| RVA | Finding |
| --- | --- |
| `0x876880` | Native `ToggleFreeCameraMode`, correctly mapped by Address Library ID `49876`. |
| `0x8768C8`–`0x8768E2` | Activation sets `RAX=0x30`, clears `RCX`, then stores the source translation to absolute addresses `0x34`, `0x30`, and `0x38`. The first store, `movss [0x34], xmm1` at `0x8768CF`, matches the reported access violation. |
| `0x8768EB`–`0x876905` | Activation also passes a null state to the rotation helper and omits the state switch. Replacing only the invalid stores would not complete activation. |
| `0x876912` | The native function tests `freezeTime` after the invalid stores. Changing that argument cannot avoid the activation crash. |
| `0x8757FD`–`0x8758C4` | The VR constructor allocates a `0x50`-byte `FreeCameraState`, initializes ID `3`, and stores it at `PlayerCamera+0xD8`: VR `cameraStates[3]`, whose array begins at `+0xC0`. |
| `0x172CFC0` | Free-camera vtable: Begin `0x8738A0`, End `0x873950`, VR update thunk `0x209160`, Update `0x8739E0`, GetRotation `0x873AF0`, GetTranslation `0x873B30`. |
| `0x873B50` | Native quaternion-to-free-camera rotation helper. It writes pitch at state `+0x3C` and yaw at `+0x40`; translation occupies `+0x30`–`+0x38`. |
| `0x505F60` | `TESCamera::SetState`, Address Library ID `32290`. Calls the previous state's End, transfers the reference, then calls the new state's Begin. |
| `0x72C030` | The VR mapping of CommonLib `PushCameraState` is a no-op stub. It cannot activate free camera. |
| `0x878F10` | Native exit pops the engine's temporary return-state stack, falling back to first-person if empty. A replacement using its own retained return state must restore that state directly. |

The native toggle's `0xC3` function bytes are identical across four preserved live images. This is an engine activation defect, not evidence of an NR setting or missing controller causing the crash.

The remaining camera pipeline exists in VR. `PlayerCamera::Update` (`0x875F00`) reaches `TESCamera::Update` (`0x505DD0`) and the active state's VR update slot. Free-camera Update writes the camera node transform. The VR late update also reaches the state's update through `0x505EF0`; the free-camera thunk forwards to its real Update. The stereo camera path (`0xCAC700`/`0xCAC900`) composes the camera world transform with each eye's eye-to-head transform. These observations support the implementation path; they do not replace a rendered stereo test.

## Implementation constraints

Read source rotation and translation through VR virtual slots **5 and 6**, respectively. Their flat-game slots differ. Retain the prior camera state strongly, initialize the actual free-camera state, and use `SetState` for entry and restoration. Do not use the broken native toggle or the VR `PushCameraState` stub.

The `camera` tool leaves `freezeTime` untouched. Its free camera can therefore run while the game continues normally, or while an independently established freeze remains active. Free-camera pitch/yaw are native free-camera angles in radians; arbitrary combined Euler angles reported for a world matrix are not automatically interchangeable with that convention.

This fix is scoped to DevBench's `camera` actions. Native console **`tfc` and `ToggleFlyCam` remain unsafe** unless separately repaired: their command handler at `0x326280` calls the same broken toggle at `0x32630F`.

## Focused in-game test

The opt-in HTTP regression module `tests/http/test_camera_freecam.py` checks repeated enable/drive/restore, idempotent requests, persistent camera-node transforms, and the player's unchanged position. Set `DEVBENCH_TEST_FREECAM=1` and `DEVBENCH_URL` for the intended instance, then run `python -m pytest tests/http/test_camera_freecam.py -v`. The module skips before discovery when disabled and checks `freeCamBackend: "vr-state"` before mutation when enabled. A visually inspected stereo pair remains necessary to qualify presentation.

Use an already loaded scene with visible nearby geometry. A Valve null HMD is sufficient; controllers are not required. Capture both eyes and the player pose, and retain the initial camera state and freeze status where diagnostics expose them.

1. Read `camera {"action":"get"}`, then enable with `camera {"action":"freecam","on":true}`. Verify `freeCam:true`, continuing game frames, and no unexpected initial view jump.
2. Use `camera {"action":"drive",...}` to establish a nearby position with explicit pitch/yaw. Capture both eyes. Move 100 world units on one axis while keeping those angles fixed. Verify visible parallax in both eyes and an unchanged player position.
3. Keep the position fixed and change yaw by `0.25` radians, then pitch by `0.15` radians. Verify that the rendered view responds in both eyes, with consistent stereo alignment.
4. Disable with `camera {"action":"freecam","on":false}`. Verify `freeCam:false`, restoration of the prior camera mode, and continued normal VR rendering.
5. Repeat the enable/drive/disable sequence three times. Also repeat an already enabled request and an already disabled request; neither should toggle the state unexpectedly or replace the retained return state.
6. Confirm freeze status is unchanged throughout. Run the same round trip once with an independently established freeze if that behavior is being qualified.
7. After leaving free camera, verify normal gameplay camera updates still work. If lifecycle cleanup is changed, additionally test loading a save while free camera is active and confirm the previous session's retained state is not restored into the new session.

Passing means visible camera movement with the player stationary, coherent stereo, reliable restoration, and no crash. A successful tool receipt or changing state ID alone is insufficient.
