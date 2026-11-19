#!/usr/bin/env bash
# The release script's own tests, on any machine: stand-ins for Apple's tools (mock-bin/) let
# the whole flow run, then each fail-closed gate is tripped once and must stop the release.
# What these cannot test is Apple's real behaviour: that runs only on a Mac
# (docs/release/RELEASE_RUNBOOK.md).
#   packaging/release/tests/run-mock-release.sh
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
src="$(cd "$here/../../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
pass=0 fail=0
# RELEASE_BASH=/bin/bash on a Mac runs the release under macOS's own bash 3.2 (as on the
# owner's Mac), which the scripts must stay compatible with.
release_bash="${RELEASE_BASH:-bash}"

fresh_repo() {
    rm -rf "$work/repo"
    mkdir -p "$work/repo/docs"
    cp -a "$src/packaging" "$work/repo/"
    cp -a "$src/docs/release" "$work/repo/docs/" 2> /dev/null || true
    (cd "$work/repo" && git init -q && git -c user.email=t@t -c user.name=t add -A \
        && git -c user.email=t@t -c user.name=t commit -qm test && git remote add origin https://github.com/example/osp.git)
}
release() {
    (cd "$work/repo" && PATH="$here/mock-bin:$PATH" PLISTBUDDY="$here/mock-bin/_plistbuddy" HOME="$work/home" \
        "$release_bash" packaging/release/andor-release.sh --product osp "$@") > "$work/out.txt" 2>&1
}
out_dir() { echo "$work/repo/dist/release/andor-osp/0.9.0-beta.1"; }
expect() {   # expect <name> <0|fail> <grep pattern in output or ->
    local name="$1" want="$2" pattern="$3" rc="$4"
    if { [ "$want" = 0 ] && [ "$rc" = 0 ]; } || { [ "$want" = fail ] && [ "$rc" != 0 ]; }; then
        if [ "$pattern" = - ] || grep -q -- "$pattern" "$work/out.txt"; then
            echo "PASS  $name"; pass=$((pass + 1)); return
        fi
    fi
    echo "FAIL  $name (exit $rc)"; tail -15 "$work/out.txt" | sed 's/^/      /'; fail=$((fail + 1))
}

# 1. The whole release, every gate passing.
fresh_repo; release; rc=$?
expect "full release" 0 "Installer:" "$rc"
d="$(out_dir)"
for f in ANDOR-OSP-0.9.0-beta.1-macOS-universal.pkg ANDOR-OSP-0.9.0-beta.1-macOS-universal.pkg.sha256 \
         release-manifest.json VERIFICATION_REPORT.md RELEASE_NOTES.md logs/release.log logs/notary-log.json logs/payload-files.txt; do
    if [ -e "$d/$f" ]; then echo "PASS  produces $f"; pass=$((pass + 1)); else echo "FAIL  missing $f"; fail=$((fail + 1)); fi
done
if (cd "$d" && shasum -a 256 -c ANDOR-OSP-0.9.0-beta.1-macOS-universal.pkg.sha256 > /dev/null); then
    echo "PASS  checksum matches the installer"; pass=$((pass + 1)); else echo "FAIL  checksum"; fail=$((fail + 1)); fi
python3 - "$d/release-manifest.json" <<'PY' && { echo "PASS  manifest content"; pass=$((pass + 1)); } || { echo "FAIL  manifest content"; fail=$((fail + 1)); }
import json, sys
m = json.load(open(sys.argv[1]))
assert m["kind"] == "andor.release-manifest" and m["schemaVersion"] == 1
assert m["version"] == {"semver": "0.9.0-beta.1", "marketing": "0.9.0", "prerelease": "beta.1", "build": 1, "bundleVersion": "0.9.0.1"}, m["version"]
assert m["channel"] == "beta" and m["releaseReady"] is True
assert m["artifacts"][0]["notarization"] == {"submissionId": "mock-0001", "status": "Accepted", "stapled": True}
assert len(m["artifacts"][0]["sha256"]) == 64 and m["artifacts"][0]["bytes"] > 0
assert [c["id"] for c in m["components"]] == ["au", "vst3", "app"]
assert m["product"]["audioUnit"] == {"type": "aumu", "subtype": "Osp1", "manufacturer": "Ospx"}
assert len(m["source"]["commit"]) == 40 and m["source"]["clean"] is True
PY
grep -q 'start_selected="false"' "$d/logs/distribution.xml" && grep -q 'enable_currentUserHome="false"' "$d/logs/distribution.xml" \
    && { echo "PASS  installer: legacy move unticked, system domain only"; pass=$((pass + 1)); } || { echo "FAIL  distribution.xml"; fail=$((fail + 1)); }
if ls -l "$d/ANDOR-OSP-0.9.0-beta.1-macOS-universal.pkg" | cut -c1-10 | grep -q w; then echo "FAIL  installer left writable"; fail=$((fail + 1)); else echo "PASS  installer read-only after its checksum"; pass=$((pass + 1)); fi

# 2. A released installer is never overwritten.
release; expect "refuses to overwrite a released installer" fail "already exists" "$?"

# 3-9. Each gate stops the release, and nothing lands in the final folder.
gate() {   # gate <name> <pattern> <env...> -- <args...>
    local name="$1" pattern="$2"; shift 2
    local -a envs=() args=()
    while [ $# -gt 0 ] && [ "$1" != "--" ]; do envs+=("$1"); shift; done
    [ "${1:-}" = "--" ] && shift
    args=("$@")
    fresh_repo
    (cd "$work/repo" && env ${envs[@]+"${envs[@]}"} PATH="$here/mock-bin:$PATH" PLISTBUDDY="$here/mock-bin/_plistbuddy" HOME="$work/home" \
        "$release_bash" packaging/release/andor-release.sh --product osp ${args[@]+"${args[@]}"}) > "$work/out.txt" 2>&1
    local rc=$?
    expect "$name" fail "$pattern" "$rc"
    if [ -e "$(out_dir)" ]; then echo "FAIL  $name left a final release folder"; fail=$((fail + 1)); fi
}
gate "notarization Invalid stops"       "not Accepted"            MOCK_NOTARY_STATUS=Invalid
gate "notarization issues stop"         "Apple reported issues"   MOCK_NOTARY_ISSUE=unsigned-binary
gate "factory authoring ON stops"       "customer configuration"  MOCK_AUTHORING=ON
gate "failing regression suite stops"   "command failed: ctest"   MOCK_TESTS_FAIL=1
gate "forbidden payload file stops"     "forbidden files"         MOCK_EXTRA_FILE=take.wav
gate "wrong team stops"                 "TeamIdentifier"          MOCK_TEAM=XXXXXXXXXX
gate "signing failure stops"            "command failed: codesign" MOCK_FAIL_SIGN=1
gate "two matching certificates stop"   "expected one"            MOCK_TWO_IDENTITIES=1
fresh_repo; echo change >> "$work/repo/packaging/release/products/osp/CHANGELOG.md"
release; expect "uncommitted changes stop" fail "uncommitted changes" "$?"

# 10. Development options build, but are marked NOT release-ready.
fresh_repo; release --skip-tests; rc=$?
expect "--skip-tests builds" 0 "NOT RELEASE-READY" "$rc"
python3 -c 'import json,sys; m=json.load(open(sys.argv[1])); assert m["releaseReady"] is False' "$(out_dir)/release-manifest.json" \
    && { echo "PASS  --skip-tests marked not release-ready"; pass=$((pass + 1)); } || { echo "FAIL  --skip-tests manifest"; fail=$((fail + 1)); }

# 11. The legacy postinstall script is valid bash and only names the configured paths.
fresh_repo; release > /dev/null; script="$(find "$work/repo/build-release" -path '*scripts-legacy/postinstall' | head -1)"
if [ -n "$script" ] && bash -n "$script" && grep -q '"Library/Audio/Plug-Ins/Components/OSP.component"' "$script" && ! grep -q 'rm ' "$script"; then
    echo "PASS  legacy move script: valid, exact paths, no deletion"; pass=$((pass + 1)); else echo "FAIL  legacy move script"; fail=$((fail + 1)); fi

echo
echo "$pass passed, $fail failed"
[ "$fail" = 0 ]
