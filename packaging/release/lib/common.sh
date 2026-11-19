#!/usr/bin/env bash
# Shared by the ANDOR release scripts: logging, fail-closed helpers, the release record.
# Sourced; expects `set -euo pipefail` in the caller.

# -- output ----------------------------------------------------------------------------------
if [ -t 1 ]; then
    c_bold=$'\e[1m'; c_red=$'\e[31m'; c_green=$'\e[32m'; c_yellow=$'\e[33m'; c_off=$'\e[0m'
else
    c_bold=""; c_red=""; c_green=""; c_yellow=""; c_off=""
fi

step() { printf '\n%s== %s%s\n' "$c_bold" "$*" "$c_off"; log "== $*"; }
info() { printf '   %s\n' "$*"; log "   $*"; }
ok()   { printf '   %sOK%s %s\n' "$c_green" "$c_off" "$*"; log "   OK $*"; }
warn() { printf '   %sWARN%s %s\n' "$c_yellow" "$c_off" "$*" >&2; log "   WARN $*"; WARNINGS+=("$*"); }
die()  { printf '\n%sRELEASE STOPPED:%s %s\n' "$c_red$c_bold" "$c_off" "$*" >&2; log "STOPPED: $*"; exit 1; }

WARNINGS=()
LOG_FILE="${LOG_FILE:-/dev/null}"
log() { printf '%s %s\n' "$(date -u +%H:%M:%S)" "$*" >> "$LOG_FILE"; }

# Runs a command, its output to the screen and the release log; any failure stops the release.
run() {
    log "\$ $*"
    if ! "$@" 2>&1 | tee -a "$LOG_FILE"; then
        die "command failed: $*"
    fi
}
# Runs a command quietly into a file (and the log); fails closed.
capture() {
    local out="$1"; shift
    log "\$ $* > $out"
    if ! "$@" > "$out" 2>&1; then
        cat "$out" >&2
        cat "$out" >> "$LOG_FILE"
        die "command failed: $*"
    fi
    cat "$out" >> "$LOG_FILE"
}

need() { command -v "$1" > /dev/null 2>&1 || die "missing tool: $1${2:+ ($2)}"; }

PLISTBUDDY="${PLISTBUDDY:-/usr/libexec/PlistBuddy}"   # (a stand-in in the script's own tests)
sha256_of() { shasum -a 256 "$1" | cut -d' ' -f1; }
size_of()   { wc -c < "$1" | tr -d ' '; }
human()     { awk -v b="$1" 'BEGIN { split("B KB MB GB", u); i = 1; while (b >= 1024 && i < 4) { b /= 1024; i++ } printf (i == 1 ? "%d %s" : "%.1f %s"), b, u[i] }'; }
dir_size()  { du -sk "$1" | awk '{ print $1 * 1024 }'; }

# KEY=value files (version.env): read without executing anything in them.
read_kv() {
    local file="$1" key="$2" line
    line="$(grep -E "^${key}=" "$file" | tail -1 || true)"
    printf '%s' "${line#*=}"
}

# -- the release record (key=value lines; the manifest and report are written from it) -------
RECORD_FILE=""
record() { printf '%s=%s\n' "$1" "$2" >> "$RECORD_FILE"; log "record $1=$2"; }
