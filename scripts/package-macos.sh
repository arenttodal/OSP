#!/usr/bin/env bash
# Superseded by the ANDOR release command, which signs inside-out, packages, notarizes, staples,
# verifies and writes the checksum, manifest and report (docs/release/RELEASE_RUNBOOK.md).
# Kept so the old entry point still works: it forwards every option.
exec "$(cd "$(dirname "$0")/.." && pwd)/packaging/release/andor-release.sh" --product osp "$@"
