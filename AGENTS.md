# Agent instructions

These instructions apply to the maintained `ParticleTroned/devbench` fork.

## Pull requests and Git safety

- Make all changes on a feature branch and open a PR targeting this
  repository's `main`. This includes upstream synchronization and release
  tooling. Do not push changes directly to `main`.
- Push only to `origin` (`ParticleTroned/devbench`). The `upstream` remote
  (`alandtse/devbench`) is fetch-only.
- Preserve user changes, build outputs, and both Git histories. Do not
  force-push or rebase shared branches.
- For upstream synchronization, branch from the fork's `main`, merge
  `upstream/main` into that feature branch, resolve conflicts while
  preserving fork behavior, validate, and open the PR. Merge the PR with
  a merge commit; do not squash away upstream ancestry.
- Use Conventional Commit subjects and PR titles. Agent-created commits
  must include specific, wrapped `Rationale:` and `Implementation:`
  sections. Check the configured Git identity and preserve attribution.
- Include the reason, resulting behavior, compatibility implications,
  and exact validation evidence in each PR. Identify skipped or blocked
  checks explicitly.

## Release identity

- Upstream owns the numeric version in `xmake.lua` and upstream `v*`
  tags. Retain that version when merging upstream.
- Fork release automation owns the independent `pt-v*` sequence. Do not
  hand-edit a fork version or manually create release tags.
- Preserve the combined runtime identity derived from the upstream
  version and first-parent fork tag. Follow the release rules in
  [README.md](README.md#fork-releases-and-upstream-synchronization).

## Validation

- Scope formatting and pre-commit checks to changed files or revisions.
- For runtime changes, build the universal SE/AE/VR plugin and run the
  affected C++ and HTTP tests. Keep registered descriptions, schemas,
  and bridge fallbacks consistent when changing tool contracts.
- Report compilation, offline tests, and live-game tests separately.
  Do not claim runtime validation from a successful build alone.
