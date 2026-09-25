# Upstream DevBench 1.22.0 synchronization

The synchronization was integrated into the fork's `main` on 2026-09-25
before a PR was requested. This record documents that existing change;
it does not introduce another runtime implementation. Future changes,
including upstream synchronization, require a PR to the fork's `main`
under [AGENTS.md](../AGENTS.md).

## Source identity and review

| Role                               | Commit                                     |
| ---------------------------------- | ------------------------------------------ |
| Fork before synchronization        | `5734b262f9274606b07dfb17842d1bb7906ad058` |
| Imported upstream main             | `32823e465c18a676afc9048ac948aa22a616abd0` |
| Ancestry-preserving merge          | `2b3dc7dd9179e8bcf11f4156e8700415228c891f` |
| Fork release automation correction | `6066fc69010e96b01e361d4761ebaecfa2b8faaa` |

Review the
[complete synchronization comparison](https://github.com/ParticleTroned/devbench/compare/5734b262f9274606b07dfb17842d1bb7906ad058...6066fc69010e96b01e361d4761ebaecfa2b8faaa)
and the
[merge commit](https://github.com/ParticleTroned/devbench/commit/2b3dc7dd9179e8bcf11f4156e8700415228c891f).
Both parent histories remain intact.

## Integration behavior

The merge imports upstream 1.22.0 and preserves the fork's four-hour
capture window, correlation guard on recording stop, physical input
observation discovery, 30-minute replay bound, and independent fork
release identity.

The shared upstream free-camera implementation includes the existing
VR ownership and load-recovery safeguards. It replaces the duplicate
VR camera implementation and uses the upstream superset of camera tests.

Console-event compaction runs under the recording mutex within retryable
stop persistence. Replay timing is validated both before upstream wait
scaling and after scaling. Raw invalid waits therefore cannot be hidden
by normalization. Tool descriptions, schemas, and bridge fallbacks
describe the combined behavior.

## Release and install package

The immutable fork tag `pt-v1.17.0` points to the merge commit and reports
runtime identity `1.22.0+pt.1.17.0`. Upstream's numeric version remains
`1.22.0`. The
[published release](https://github.com/ParticleTroned/devbench/releases/tag/pt-v1.17.0)
contains the universal SE/AE/VR DLL, debugging symbols, bridge executable,
and bundled recordings.

The initial release workflow created the tag and draft, then failed
because its GitHub plugin looked up an imported upstream PR number in
the fork. The follow-up release configuration disables issue comments
and released labels in the fork only. The existing tag was retained,
and its package was built and published with verified source identity.

The DLL and symbols came from a local tagged universal `releasedbg`
build. The bridge came from successful CI run `36115131762`, artifact
`10854742513`, at the same merge commit. The later release configuration
correction does not change product sources.

| Artifact                                        | SHA-256                                                            |
| ----------------------------------------------- | ------------------------------------------------------------------ |
| `devbench.dll`                                  | `204fa827b08cfa293095526d69b7fde6a89a94cbd04bb67f7d511ecfb2af467e` |
| `devbench-bridge.exe`                           | `edab5e45bd8cf7bbc2cf5732e7a5c967c672fc295243ed9e36df7036bb0517ae` |
| Published `devbench-pt-v1.17.0.7z`              | `399a36ab86d58b6f3467241eb696042045587f6c1e2e8f0ae7825d7d758f1bcc` |
| MO2 `DevBench_AIO-1.22.0-pt.1.17.0-20260925.7z` | `9572665d068956e342a084ebcd1977505e44f3c3246e45bf54e77c741ecb6199` |

The MO2 archive places `SKSE` at its root. Its six files match the
published release receipt byte-for-byte. The archive is 35,154,260 bytes;
its adjacent JSON receipt and SHA-256 file retain the package provenance.

## Validation evidence

- `xmake build --yes -j 4 devbench-tests` succeeded;
  `devbench-tests.exe` reported 166 cases and zero failures.
- `xmake build --yes -j 4 devbench` produced the universal `releasedbg`
  DLL; the final recording integration rebuilt successfully.
- `node --test tests/fork-release.test.cjs`: 11 passed.
- `xmake lua tests/fork-version.lua`: passed.
- Offline HTTP camera/input/shutdown tests: 36 passed, seven live-game
  camera cases skipped.
- Focused pre-commit hooks, bridge TypeScript `--noEmit`, tools-fallback
  synchronization, and recording/camera description and schema parity
  checks passed.
- The installed `@semantic-release/github` 11.0.6 success/failure hooks
  passed a mock-API probe that rejects PR lookups and issue mutations;
  only repository identity was requested.
- [Merge Build run 36115131762](https://github.com/ParticleTroned/devbench/actions/runs/36115131762)
  and [merge Lint run 36115131495](https://github.com/ParticleTroned/devbench/actions/runs/36115131495)
  passed.
- The MO2 archive passed `7z t`. A fresh extraction verified all six
  file sizes and SHA-256 hashes against the tagged release receipt.

No game deployment or live SE, AE, or VR runtime test was performed as
part of this synchronization or package preparation.
