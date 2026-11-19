#!/usr/bin/env bash
# Checks of the finished installer: its signature, that its payload is exactly the signed
# bundles, nothing forbidden in it, Gatekeeper's verdict, and (opt-in) a real installation.

# The product archive is signed with the Developer ID Installer certificate of our team.
verify_installer_signature() {
    local pkg="$1" out="$OUT_DIR/logs/pkgutil-check-signature.txt"
    capture "$out" pkgutil --check-signature "$pkg"
    grep -q "Developer ID Installer: .*(${TEAM_ID})" "$out" || die "installer is not signed with our Developer ID Installer certificate"
    grep -qi "signed by a developer certificate issued by Apple" "$out" || die "installer signature is not trusted (see $out)"
    ok "installer signed: Developer ID Installer, team $TEAM_ID"
}

# Expands the installer and checks every payload bundle: valid signature, and the same
# CDHash as the bundle signed in the stage (nothing changed after signing). Also lists the
# payload and refuses anything forbidden.
verify_payload() {
    local pkg="$1" expanded="$WORK/expanded" line choice bundle location identifier title ent description
    rm -rf "$expanded"
    run pkgutil --expand-full "$pkg" "$expanded"
    : > "$OUT_DIR/logs/payload-files.txt"
    for line in "${COMPONENTS[@]}"; do
        IFS='|' read -r choice bundle location identifier title ent description <<< "$line"
        local name shipped staged a b
        name="$(basename "$bundle")"
        shipped="$expanded/$choice.pkg/Payload/$name"
        staged="$STAGE/$choice/$name"
        [ -d "$shipped" ] || die "$choice.pkg does not contain $name"
        run codesign --verify --deep --strict --verbose=2 "$shipped"
        a="$(codesign -dv --verbose=4 "$staged" 2>&1 | awk -F= '/^CDHash=/ { print $2 }')"
        b="$(codesign -dv --verbose=4 "$shipped" 2>&1 | awk -F= '/^CDHash=/ { print $2 }')"
        [ -n "$a" ] && [ "$a" = "$b" ] || die "$name in the installer differs from the signed bundle ($a vs $b)"
        (cd "$expanded/$choice.pkg/Payload" && find . -type f | sed "s|^\./|$location/|") >> "$OUT_DIR/logs/payload-files.txt"
        record "component_${choice}_size" "$(dir_size "$shipped")"
        record "component_${choice}_cdhash" "$b"
        ok "$name: shipped exactly as signed (CDHash $b)"
    done
    local pattern hits=""
    for pattern in "${FORBIDDEN_PAYLOAD_PATTERNS[@]}"; do
        hits+="$(grep -E "$pattern" "$OUT_DIR/logs/payload-files.txt" || true)"
    done
    [ -z "$hits" ] || die "forbidden files in the payload:
$hits"
    ok "payload: $(wc -l < "$OUT_DIR/logs/payload-files.txt" | tr -d ' ') files, none forbidden (logs/payload-files.txt)"
}

# Gatekeeper's verdict on the stapled installer, and pkgutil's notarization line.
verify_gatekeeper() {
    local pkg="$1" out="$OUT_DIR/logs/spctl-install.txt"
    capture "$out" spctl --assess --type install --verbose=4 "$pkg"
    grep -q "accepted" "$out" || die "Gatekeeper does not accept the installer (see $out)"
    grep -q "Notarized Developer ID" "$out" || die "Gatekeeper accepts it, but not as notarized (see $out)"
    capture "$OUT_DIR/logs/pkgutil-check-signature-stapled.txt" pkgutil --check-signature "$pkg"
    ok "Gatekeeper: accepted, source=Notarized Developer ID"
    record gatekeeper "accepted (Notarized Developer ID)"
}

# --install-test: installs the final installer on this Mac (administrator password), then
# checks what landed: paths, signatures, Gatekeeper on the app, auval, pluginval, a launch.
# The installer itself is not touched.
install_test() {
    local pkg="$1" report="$OUT_DIR/logs/install-test.txt" line choice bundle location identifier title ent description
    : > "$report"
    info "installing $(basename "$pkg") (macOS asks for an administrator password)"
    run sudo installer -pkg "$pkg" -target / -verboseR
    for line in "${COMPONENTS[@]}"; do
        IFS='|' read -r choice bundle location identifier title ent description <<< "$line"
        local installed="$location/$(basename "$bundle")"
        [ -d "$installed" ] || die "not installed: $installed"
        run codesign --verify --deep --strict --verbose=2 "$installed"
        echo "installed: $installed" >> "$report"
    done
    record install_test "installed"

    # The standalone, as Gatekeeper sees an app the person opens.
    local app
    app="$(installed_path app)"
    capture "$OUT_DIR/logs/spctl-app.txt" spctl --assess --type execute --verbose=4 "$app"
    grep -q "Notarized Developer ID" "$OUT_DIR/logs/spctl-app.txt" || die "Gatekeeper does not see $app as notarized"
    ok "Gatekeeper: $app accepted (Notarized Developer ID)"

    # Copies left by earlier test builds, which a host would find beside this one.
    local rel dupes=""
    for rel in "${LEGACY_USER_PATHS[@]}"; do
        [ -e "$HOME/$rel" ] && dupes+="~/$rel "
    done
    if [ -n "$dupes" ]; then
        warn "earlier per-user copies still present (hosts may load them instead): $dupes"
        record duplicates "$dupes"
    else
        record duplicates "none"
    fi

    # auval: what Logic runs before it lists a plug-in.
    killall -9 AudioComponentRegistrar 2> /dev/null || true
    sleep 2
    if auval -v "$AU_TYPE" "$AU_SUBTYPE" "$AU_MANUFACTURER" > "$OUT_DIR/logs/auval.txt" 2>&1 \
        && grep -q "AU VALIDATION SUCCEEDED" "$OUT_DIR/logs/auval.txt"; then
        ok "auval: AU VALIDATION SUCCEEDED"
        record auval "passed"
    else
        tail -30 "$OUT_DIR/logs/auval.txt" >&2
        die "auval failed (logs/auval.txt)"
    fi

    run_pluginval "$(installed_path vst3)" vst3
    run_pluginval "$(installed_path au)" au

    # The standalone starts, runs for ten seconds and quits when asked.
    open -a "$app"
    sleep 10
    if pgrep -x "$STANDALONE_PROCESS" > /dev/null; then
        ok "standalone launched and is running"
        osascript -e "quit app \"$STANDALONE_PROCESS\"" > /dev/null 2>&1 || true
        sleep 3
        pkill -x "$STANDALONE_PROCESS" 2> /dev/null || true
        record standalone_launch "passed"
    else
        die "the standalone did not stay running (see Console.app for a crash report)"
    fi
}

installed_path() {
    local want="$1" line choice bundle location identifier title ent description
    for line in "${COMPONENTS[@]}"; do
        IFS='|' read -r choice bundle location identifier title ent description <<< "$line"
        if [ "$choice" = "$want" ]; then
            echo "$location/$(basename "$bundle")"
            return
        fi
    done
}

# pluginval (Tracktion), pinned: a stricter host than auval. Not available offline: recorded
# as not run, never as passed.
run_pluginval() {
    local plugin="$1" kind="$2" tools="$WORK/pluginval" out="$OUT_DIR/logs/pluginval-$kind.txt"
    if [ ! -x "$tools/pluginval.app/Contents/MacOS/pluginval" ]; then
        mkdir -p "$tools"
        if ! curl -fsSL -o "$tools/pluginval.zip" \
            "https://github.com/Tracktion/pluginval/releases/download/$PLUGINVAL_VERSION/pluginval_macOS.zip" \
            || ! ditto -x -k "$tools/pluginval.zip" "$tools"; then
            warn "pluginval $PLUGINVAL_VERSION could not be downloaded: $kind not validated by pluginval"
            record "pluginval_$kind" "not run (download failed)"
            return
        fi
    fi
    if "$tools/pluginval.app/Contents/MacOS/pluginval" --strictness-level "$PLUGINVAL_STRICTNESS" \
        --skip-gui-tests --validate-in-process --validate "$plugin" > "$out" 2>&1; then
        ok "pluginval ($kind, strictness $PLUGINVAL_STRICTNESS): passed"
        record "pluginval_$kind" "passed (strictness $PLUGINVAL_STRICTNESS)"
    else
        tail -30 "$out" >&2
        die "pluginval failed on $plugin (logs/pluginval-$kind.txt)"
    fi
}
