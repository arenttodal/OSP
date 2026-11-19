#!/usr/bin/env bash
# Inside-out Developer ID signing. Never `codesign --deep` for signing: each nested piece of
# code is signed on its own, deepest first, then its container, so every signature is made
# with the right flags and the outer seal covers finished inner signatures. (--deep is used
# below only to VERIFY.)

# (bash 3.2, macOS's own: no mapfile, no associative arrays, guarded empty arrays.)

# codesign options every signature here gets: the hardened runtime and a secure timestamp.
set_codesign_args() {
    CODESIGN_ARGS=(--force --timestamp --options runtime --sign "$APP_IDENTITY")
    if [ -n "${SIGN_KEYCHAIN:-}" ]; then
        CODESIGN_ARGS+=(--keychain "$SIGN_KEYCHAIN")
    fi
}

is_macho() { file -b "$1" 2> /dev/null | grep -q "Mach-O"; }

# The nested code of a bundle, deepest first: frameworks, bundles, apps, XPC services and any
# loose Mach-O file (dylibs, helpers), excluding the bundle's own main executable.
nested_code() {
    local bundle="$1" main="$2"
    {
        find "$bundle/Contents" -mindepth 1 \( -name "*.framework" -o -name "*.bundle" -o -name "*.app" \
            -o -name "*.appex" -o -name "*.xpc" -o -name "*.plugin" -o -name "*.component" -o -name "*.vst3" \) -type d -print
        find "$bundle/Contents" -type f -print | while read -r f; do
            [ "$f" = "$main" ] && continue
            is_macho "$f" && echo "$f"
        done
    } | awk '{ print gsub("/", "/") "\t" $0 }' | sort -rn | cut -f2- | awk '!seen[$0]++'
}

# sign_bundle <bundle> <entitlements file or ->
sign_bundle() {
    local bundle="$1" entitlements="$2"
    local exe main
    exe="$("$PLISTBUDDY" -c "Print :CFBundleExecutable" "$bundle/Contents/Info.plist")"
    main="$bundle/Contents/MacOS/$exe"
    [ -f "$main" ] || die "$bundle: no main executable $main"

    # Finder metadata and resource forks break a signature ("detritus not allowed").
    xattr -cr "$bundle"
    find "$bundle" -name ".DS_Store" -delete

    set_codesign_args
    local nested
    while IFS= read -r nested; do
        [ -n "$nested" ] || continue
        info "sign (nested) ${nested#"$bundle"/}"
        run codesign "${CODESIGN_ARGS[@]}" "$nested"
    done < <(nested_code "$bundle" "$main")

    if [ "$entitlements" != "-" ]; then
        info "sign $(basename "$bundle") with $(basename "$entitlements")"
        run codesign "${CODESIGN_ARGS[@]}" --entitlements "$entitlements" "$bundle"
    else
        info "sign $(basename "$bundle") (no entitlements: a plug-in runs with its host's)"
        run codesign "${CODESIGN_ARGS[@]}" "$bundle"
    fi
}

# verify_bundle <bundle> <expected identifier> <entitlement that must be present, or ->
# Fails closed on: an invalid or unsealed signature, the wrong identifier or team, no hardened
# runtime, no secure timestamp, a missing architecture or the wrong deployment target.
verify_bundle() {
    local bundle="$1" identifier="$2" entitlement="$3" name details
    name="$(basename "$bundle")"
    run codesign --verify --deep --strict --verbose=2 "$bundle"
    details="$(codesign -dv --verbose=4 "$bundle" 2>&1)"
    log "$details"
    grep -q "^Identifier=${identifier}$" <<< "$details" || die "$name: identifier is not $identifier"
    grep -q "^TeamIdentifier=${TEAM_ID}$" <<< "$details" || die "$name: TeamIdentifier is not $TEAM_ID"
    grep -q "^Authority=Developer ID Application: " <<< "$details" || die "$name: not signed with a Developer ID Application certificate"
    grep -Eq "flags=0x[0-9a-f]*\(.*runtime.*\)" <<< "$details" || die "$name: hardened runtime is off"
    grep -q "^Timestamp=" <<< "$details" || die "$name: no secure timestamp"
    if [ "$entitlement" != "-" ]; then
        codesign -d --entitlements - --xml "$bundle" 2> /dev/null | grep -q "$entitlement" \
            || die "$name: missing entitlement $entitlement"
    fi
    ok "$name: valid, $identifier, team $TEAM_ID, hardened runtime, timestamped"
}

# The main executable carries both architectures and the expected deployment target.
verify_architectures() {
    local bundle="$1" min="$2" exe main archs minos
    exe="$("$PLISTBUDDY" -c "Print :CFBundleExecutable" "$bundle/Contents/Info.plist")"
    main="$bundle/Contents/MacOS/$exe"
    archs="$(lipo -archs "$main")"
    for a in arm64 x86_64; do
        grep -qw "$a" <<< "$archs" || die "$(basename "$bundle"): $a missing (has: $archs)"
    done
    for a in arm64 x86_64; do
        minos="$(vtool -arch "$a" -show-build "$main" 2> /dev/null | awk '/minos/ { print $2; exit }')"
        [ -n "$minos" ] || die "$(basename "$bundle"): no LC_BUILD_VERSION for $a"
        [ "$minos" = "$min" ] || die "$(basename "$bundle"): $a deployment target $minos, expected $min"
    done
    ok "$(basename "$bundle"): universal ($archs), macOS $min and later"
}
