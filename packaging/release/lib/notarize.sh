#!/usr/bin/env bash
# Notarization: submit, wait, keep the submission ID and Apple's log, require Accepted with no
# issues, staple, then check the stapled ticket.

notary_args() {
    NOTARY_ARGS=(--keychain-profile "$NOTARY_PROFILE")
    if [ -n "${SIGN_KEYCHAIN:-}" ]; then
        NOTARY_ARGS+=(--keychain "$SIGN_KEYCHAIN")
    fi
}

notarize_and_staple() {
    local pkg="$1" result="$OUT_DIR/logs/notary-submit.json" logjson="$OUT_DIR/logs/notary-log.json"
    notary_args
    info "submitting $(basename "$pkg") to Apple's notary service (this usually takes a few minutes)"
    # notarytool exits 0 even when Apple answers Invalid, so the answer itself is checked.
    if ! xcrun notarytool submit "$pkg" "${NOTARY_ARGS[@]}" --wait --timeout 2h --output-format json > "$result" 2>> "$LOG_FILE"; then
        cat "$result" >&2 || true
        die "notarytool submit failed (see $result and the release log)"
    fi
    local id status
    id="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("id",""))' "$result")"
    status="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("status",""))' "$result")"
    [ -n "$id" ] || die "no submission ID in $result"
    record notary_submission_id "$id"
    record notary_status "$status"
    info "submission $id: $status"

    # Apple's log, always kept: it lists every issue, accepted or not.
    if ! xcrun notarytool log "$id" "${NOTARY_ARGS[@]}" "$logjson" >> "$LOG_FILE" 2>&1; then
        die "could not fetch the notarization log for $id"
    fi
    local issues
    issues="$(python3 - "$logjson" <<'PY'
import json, sys
log = json.load(open(sys.argv[1]))
for i in log.get("issues") or []:
    print(f'{i.get("severity","?")}: {i.get("path","?")}: {i.get("message","?")}')
PY
)"
    if [ "$status" != "Accepted" ]; then
        [ -n "$issues" ] && printf '%s\n' "$issues" >&2
        die "notarization status is $status, not Accepted (log: $logjson)"
    fi
    if [ -n "$issues" ]; then
        printf '%s\n' "$issues" >&2
        record notary_issues "$(printf '%s' "$issues" | tr '\n' ';')"
        [ "${ACCEPT_NOTARY_ISSUES:-0}" = 1 ] || die "Accepted, but Apple reported issues (above, and $logjson). Fix them, or rerun with --accept-notary-issues after reviewing them."
        warn "Apple reported issues; accepted by --accept-notary-issues"
    fi
    ok "notarized: $id Accepted, no issues"

    run xcrun stapler staple "$pkg"
    run xcrun stapler validate "$pkg"
    record stapled "yes"
    ok "ticket stapled and validated"
}
