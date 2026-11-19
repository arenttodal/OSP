#!/usr/bin/env bash
# ANDOR macOS release: preflight -> fresh universal build -> full regression suite -> sign
# (inside-out, hardened runtime, timestamps) -> installer -> notarize -> staple -> verify ->
# checksum, manifest, release notes, verification report. Every mandatory step fails closed.
#
#   packaging/release/andor-release.sh --product osp [options]
#
# Options:
#   --product NAME          packaging/release/products/NAME/product.conf (or --config FILE)
#   --install-test          afterwards install the final installer on this Mac and check it
#                           (auval, pluginval, Gatekeeper on the app, a launch). Asks for an
#                           administrator password.
#   --output DIR            default: dist/release/<product>/<version>
#   --keychain FILE         sign and notarize with this keychain (CI); default: login keychain
#   --accept-notary-issues  continue when Apple accepts with issues (after reviewing them)
# Development only (the result is marked NOT release-ready in its manifest and report):
#   --skip-tests  --allow-dirty  --no-notarize
#
# Docs: docs/release/RELEASE_RUNBOOK.md (how), docs/release/OWNER_SETUP_MACOS.md (setup).
set -euo pipefail

RELEASE_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$RELEASE_DIR/../.." && pwd)"
# shellcheck source=lib/common.sh
source "$RELEASE_DIR/lib/common.sh"
source "$RELEASE_DIR/lib/sign.sh"
source "$RELEASE_DIR/lib/package.sh"
source "$RELEASE_DIR/lib/notarize.sh"
source "$RELEASE_DIR/lib/verify.sh"
source "$RELEASE_DIR/signing.conf"

PRODUCT="" CONFIG="" OUT_DIR="" SIGN_KEYCHAIN="" INSTALL_TEST=0 SKIP_TESTS=0 ALLOW_DIRTY=0 NO_NOTARIZE=0 ACCEPT_NOTARY_ISSUES=0
while [ $# -gt 0 ]; do
    case "$1" in
        --product) PRODUCT="$2"; shift 2 ;;
        --config) CONFIG="$2"; shift 2 ;;
        --output) OUT_DIR="$2"; shift 2 ;;
        --keychain) SIGN_KEYCHAIN="$2"; shift 2 ;;
        --install-test) INSTALL_TEST=1; shift ;;
        --skip-tests) SKIP_TESTS=1; shift ;;
        --allow-dirty) ALLOW_DIRTY=1; shift ;;
        --no-notarize) NO_NOTARIZE=1; shift ;;
        --accept-notary-issues) ACCEPT_NOTARY_ISSUES=1; shift ;;
        -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
[ -n "$CONFIG" ] || CONFIG="$RELEASE_DIR/products/${PRODUCT:?--product or --config}/product.conf"
[ -f "$CONFIG" ] || { echo "no product configuration: $CONFIG" >&2; exit 2; }
# shellcheck source=products/osp/product.conf
source "$CONFIG"
export ACCEPT_NOTARY_ISSUES

# -- version (one source) --------------------------------------------------------------------
VERSION="$(read_kv "$ROOT/$VERSION_FILE" PRODUCT_VERSION)"
PRERELEASE="$(read_kv "$ROOT/$VERSION_FILE" PRODUCT_PRERELEASE)"
BUILD_NUMBER="$(read_kv "$ROOT/$VERSION_FILE" PRODUCT_BUILD)"
CHANNEL="$(read_kv "$ROOT/$VERSION_FILE" PRODUCT_CHANNEL)"
MIN_MACOS="$(read_kv "$ROOT/$VERSION_FILE" PRODUCT_MIN_MACOS)"
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "$VERSION_FILE: PRODUCT_VERSION '$VERSION' is not MAJOR.MINOR.PATCH" >&2; exit 2; }
[[ "$BUILD_NUMBER" =~ ^[0-9]+$ ]] || { echo "$VERSION_FILE: PRODUCT_BUILD '$BUILD_NUMBER' is not a whole number" >&2; exit 2; }
case "$CHANNEL" in beta|stable) ;; *) echo "$VERSION_FILE: PRODUCT_CHANNEL must be beta or stable" >&2; exit 2 ;; esac
if [ "$CHANNEL" = stable ] && [ -n "$PRERELEASE" ]; then echo "a stable release has no PRODUCT_PRERELEASE" >&2; exit 2; fi
if [ "$CHANNEL" = beta ] && [ -z "$PRERELEASE" ]; then echo "a beta release needs PRODUCT_PRERELEASE (e.g. beta.1)" >&2; exit 2; fi
DISPLAY_VERSION="$VERSION${PRERELEASE:+-$PRERELEASE}"
BUNDLE_VERSION="$VERSION.$BUILD_NUMBER"
PKG_VERSION="$BUNDLE_VERSION"
CHANNEL_LINE="$([ "$CHANNEL" = beta ] && echo "Beta for invited testers" || echo "Release")"
INSTALLER_NAME="$PRODUCT_FILE_STEM-$DISPLAY_VERSION-macOS-universal.pkg"
LEGACY_PATHS_QUOTED=""
for p in ${LEGACY_USER_PATHS[@]+"${LEGACY_USER_PATHS[@]}"}; do LEGACY_PATHS_QUOTED+="\"$p\" "; done

OUT_DIR="${OUT_DIR:-$ROOT/dist/release/$PRODUCT_ID/$DISPLAY_VERSION}"
BUILD="$ROOT/build-release/$PRODUCT_ID-$DISPLAY_VERSION"
WORK="$BUILD/release-work"
STAGE="$WORK/stage"

# A validated installer is never overwritten: a new build gets a new version or build number.
if [ -e "$OUT_DIR/$INSTALLER_NAME" ]; then
    echo "$OUT_DIR/$INSTALLER_NAME already exists. Released installers are kept: raise PRODUCT_BUILD" >&2
    echo "(and PRODUCT_PRERELEASE) in $VERSION_FILE for a new build, or move that folder away deliberately." >&2
    exit 2
fi
rm -rf "$OUT_DIR.partial"
mkdir -p "$OUT_DIR.partial/logs"
FINAL_OUT_DIR="$OUT_DIR"
OUT_DIR="$OUT_DIR.partial"   # renamed to its final name only when everything passed
LOG_FILE="$OUT_DIR/logs/release.log"
RECORD_FILE="$OUT_DIR/logs/release-record.txt"
: > "$LOG_FILE"; : > "$RECORD_FILE"

NOT_READY=()
[ "$SKIP_TESTS" = 1 ] && NOT_READY+=("tests skipped")
[ "$ALLOW_DIRTY" = 1 ] && NOT_READY+=("uncommitted changes allowed")
[ "$NO_NOTARIZE" = 1 ] && NOT_READY+=("not notarized")

finish_record() {
    record warnings "$(printf '%s;' ${WARNINGS[@]+"${WARNINGS[@]}"})"
    if [ ${#NOT_READY[@]} -eq 0 ]; then
        record release_ready yes
    else
        record release_ready no
        record release_ready_reason "($(printf '%s, ' "${NOT_READY[@]}" | sed 's/, $//'))"
    fi
}

# ============================================================================================
step "Preflight: $PRODUCT_NAME $DISPLAY_VERSION (build $BUILD_NUMBER, $CHANNEL)"
[ "$(uname -s)" = Darwin ] || die "this runs on macOS (Xcode's codesign, pkgbuild, notarytool)"
for t in xcrun codesign pkgbuild productbuild pkgutil spctl lipo vtool cmake ninja git python3 shasum ditto file; do need "$t"; done
xcrun --find notarytool > /dev/null 2>&1 || die "notarytool not found (Xcode 13 or later)"
xcrun --find stapler > /dev/null 2>&1 || die "stapler not found"
XCODE="$(xcodebuild -version 2> /dev/null | tr '\n' ' ' | sed 's/ *$//')"
SDK="$(xcrun --sdk macosx --show-sdk-version 2> /dev/null || echo unknown)"
info "$XCODE, macOS SDK $SDK, $(sw_vers -productName) $(sw_vers -productVersion) on $(uname -m)"

cd "$ROOT"
COMMIT="$(git rev-parse HEAD)"
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
REPOSITORY="$(git config --get remote.origin.url | sed -E 's#^(https://|git@)github.com[:/]##; s#\.git$##; s#^.*://[^/]*/git/##')"
if [ -n "$(git status --porcelain --untracked-files=normal -- . ':!dist' ':!build*')" ]; then
    [ "$ALLOW_DIRTY" = 1 ] || die "the working tree has uncommitted changes: a release is built from a commit (git status)"
    warn "building with uncommitted changes (--allow-dirty)"
    TREE_CLEAN=no
else
    TREE_CLEAN=yes
fi
info "source: $REPOSITORY $BRANCH @ $COMMIT"

# The signing identities, found by Team ID unless named in the environment.
find_identity() {
    local kind="$1" policy="$2" found count
    found="$(security find-identity -v ${policy:+-p "$policy"} ${SIGN_KEYCHAIN:+"$SIGN_KEYCHAIN"} 2> /dev/null \
        | sed -n "s/.*\"\($kind: .* ($TEAM_ID)\)\"/\1/p" | sort -u)"
    count="$(printf '%s' "$found" | grep -c . || true)"
    [ "$count" = 1 ] || die "expected one '$kind' certificate for team $TEAM_ID in the keychain, found $count. Set ANDOR_${3}_IDENTITY to choose."
    printf '%s' "$found"
}
# (Explicit checks: a failure inside $(...) only ends the subshell.)
APP_IDENTITY="${ANDOR_APP_IDENTITY:-}"
if [ -z "$APP_IDENTITY" ]; then
    APP_IDENTITY="$(find_identity "Developer ID Application" codesigning APP)" || exit 1
fi
INSTALLER_IDENTITY="${ANDOR_INSTALLER_IDENTITY:-}"
if [ -z "$INSTALLER_IDENTITY" ]; then
    INSTALLER_IDENTITY="$(find_identity "Developer ID Installer" "" INSTALLER)" || exit 1
fi
ok "signing: Developer ID Application and Developer ID Installer for team $TEAM_ID"
if [ "$NO_NOTARIZE" = 0 ]; then
    notary_args
    xcrun notarytool history "${NOTARY_ARGS[@]}" > /dev/null 2>> "$LOG_FILE" \
        || die "notarytool profile '$NOTARY_PROFILE' does not work (xcrun notarytool history --keychain-profile $NOTARY_PROFILE)"
    ok "notarization: profile $NOTARY_PROFILE answers"
fi

record product_id "$PRODUCT_ID"; record product_name "$PRODUCT_NAME"; record bundle_id "$BUNDLE_ID"
record au_type "$AU_TYPE"; record au_subtype "$AU_SUBTYPE"; record au_manufacturer "$AU_MANUFACTURER"
record version "$VERSION"; record prerelease "$PRERELEASE"; record display_version "$DISPLAY_VERSION"
record build_number "$BUILD_NUMBER"; record bundle_version "$BUNDLE_VERSION"; record channel "$CHANNEL"
record min_macos "$MIN_MACOS"; record team_id "$TEAM_ID"
record repository "$REPOSITORY"; record branch "$BRANCH"; record commit "$COMMIT"; record tree_clean "$TREE_CLEAN"
record build_date "$(date -u +%Y-%m-%dT%H:%M:%SZ)"; record xcode "$XCODE"; record sdk "$SDK"; record build_host_arch "$(uname -m)"
record changelog "$CHANGELOG_FILE#$DISPLAY_VERSION"
grep -q "^## $DISPLAY_VERSION" "$ROOT/$CHANGELOG_FILE" || die "$CHANGELOG_FILE has no '## $DISPLAY_VERSION' section (the release notes come from it)"

# ============================================================================================
step "Factory content"
if [ -n "${FACTORY_FETCH:-}" ] && [ -x "$ROOT/$FACTORY_FETCH" ]; then
    run "$ROOT/$FACTORY_FETCH"
    PACKS="$(grep -Ev '^[[:space:]]*(#|$)' "$(dirname "$ROOT/$FACTORY_FETCH")/packs.lock" 2> /dev/null | awk '{ print $2 "@" $3 }' | tr '\n' ' ' || true)"
    record factory_packs "${PACKS:-none}"
    [ -n "$PACKS" ] || warn "no factory packs pinned in packs.lock: the installer ships the built-in starting states only"
fi

# ============================================================================================
step "Build: fresh, universal (arm64 + x86_64), Release"
rm -rf "$BUILD"
run cmake -S "$ROOT" -B "$BUILD" -G Ninja "${CMAKE_ARGS[@]}" "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" \
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=$MIN_MACOS" "-D$CMAKE_GIT_SHA_VAR=$(git rev-parse --short=12 HEAD)"
for entry in ${REQUIRED_CACHE[@]+"${REQUIRED_CACHE[@]}"}; do
    grep -qx "$entry" "$BUILD/CMakeCache.txt" || die "customer configuration: CMakeCache.txt does not have $entry"
done
record config_check "passed"
ok "customer configuration: ${REQUIRED_CACHE[*]}"
run cmake --build "$BUILD"

if [ "$SKIP_TESTS" = 0 ]; then
    step "Regression suite (ctest, the full suite)"
    run ctest --test-dir "$BUILD" --output-on-failure
    record tests "passed ($(grep -c . <<< "$(ctest --test-dir "$BUILD" -N | grep 'Test *#')") ctest entries)"
else
    warn "regression suite skipped (--skip-tests)"
    record tests "SKIPPED"
fi

# ============================================================================================
step "Stage and check the bundles"
rm -rf "$WORK"
mkdir -p "$STAGE"
ids=""
for line in "${COMPONENTS[@]}"; do
    IFS='|' read -r choice bundle location identifier title ent description <<< "$line"
    src="$BUILD/$ARTEFACTS/$bundle"
    [ -d "$src" ] || die "not built: $src"
    mkdir -p "$STAGE/$choice"
    run ditto "$src" "$STAGE/$choice/$(basename "$bundle")"
    staged="$STAGE/$choice/$(basename "$bundle")"
    plist="$staged/Contents/Info.plist"
    got_id="$("$PLISTBUDDY" -c "Print :CFBundleIdentifier" "$plist")"
    got_short="$("$PLISTBUDDY" -c "Print :CFBundleShortVersionString" "$plist")"
    got_build="$("$PLISTBUDDY" -c "Print :CFBundleVersion" "$plist")"
    [ "$got_id" = "$BUNDLE_ID" ] || die "$bundle: CFBundleIdentifier $got_id, expected $BUNDLE_ID"
    [ "$got_short" = "$VERSION" ] || die "$bundle: CFBundleShortVersionString $got_short, expected $VERSION"
    [ "$got_build" = "$BUNDLE_VERSION" ] || die "$bundle: CFBundleVersion $got_build, expected $BUNDLE_VERSION"
    if [ "$choice" = au ]; then
        au="$("$PLISTBUDDY" -c "Print :AudioComponents:0:type" -c "Print :AudioComponents:0:subtype" -c "Print :AudioComponents:0:manufacturer" "$plist" | tr '\n' ' ')"
        [ "$au" = "$AU_TYPE $AU_SUBTYPE $AU_MANUFACTURER " ] || die "AU codes are '$au', expected '$AU_TYPE $AU_SUBTYPE $AU_MANUFACTURER'"
    fi
    verify_architectures "$staged" "$MIN_MACOS"
    record "component_${choice}_bundle" "$(basename "$bundle")"
    record "component_${choice}_install_path" "$location/$(basename "$bundle")"
    record "component_${choice}_package_id" "$identifier"
    ids+="$choice "
done
record component_ids "$ids"
record architectures "arm64 + x86_64, macOS $MIN_MACOS"
record identity_check "passed ($BUNDLE_ID, $VERSION / $BUNDLE_VERSION, AU $AU_TYPE $AU_SUBTYPE $AU_MANUFACTURER)"
"$PLISTBUDDY" -c "Print :NSMicrophoneUsageDescription" "$STAGE/app/$(basename "$MIC_BUNDLE")/Contents/Info.plist" > /dev/null 2>&1 \
    || die "$MIC_BUNDLE has no NSMicrophoneUsageDescription"
ok "identity: $BUNDLE_ID, version $VERSION ($BUNDLE_VERSION), AU $AU_TYPE/$AU_SUBTYPE/$AU_MANUFACTURER"

# ============================================================================================
step "Sign (inside-out, hardened runtime, secure timestamp)"
for line in "${COMPONENTS[@]}"; do
    IFS='|' read -r choice bundle location identifier title ent description <<< "$line"
    entitlements="-"
    [ "$ent" != "-" ] && entitlements="$ROOT/$ent"
    sign_bundle "$STAGE/$choice/$(basename "$bundle")" "$entitlements"
done
for line in "${COMPONENTS[@]}"; do
    IFS='|' read -r choice bundle location identifier title ent description <<< "$line"
    must="-"
    [ "$bundle" = "$MIC_BUNDLE" ] && must="$MIC_ENTITLEMENT"
    verify_bundle "$STAGE/$choice/$(basename "$bundle")" "$BUNDLE_ID" "$must"
done
record bundle_signatures "valid (Developer ID Application, team $TEAM_ID, hardened runtime, timestamped)"

# ============================================================================================
step "Installer"
PKG="$OUT_DIR/$INSTALLER_NAME"
build_installer "$PKG"
verify_installer_signature "$PKG"
record installer_signature "Developer ID Installer, team $TEAM_ID"
verify_payload "$PKG"
record payload_check "passed (logs/payload-files.txt)"

# ============================================================================================
if [ "$NO_NOTARIZE" = 0 ]; then
    step "Notarize and staple"
    notarize_and_staple "$PKG"
    step "Gatekeeper"
    verify_gatekeeper "$PKG"
else
    warn "not notarized (--no-notarize): Gatekeeper will refuse this installer on other Macs"
    record notary_status "NOT SUBMITTED"
fi

# ============================================================================================
step "Checksum (final: nothing writes to the installer after this)"
chmod a-w "$PKG"
SHA="$(sha256_of "$PKG")"
SIZE="$(size_of "$PKG")"
printf '%s  %s\n' "$SHA" "$INSTALLER_NAME" > "$PKG.sha256"
record installer_name "$INSTALLER_NAME"; record installer_size "$SIZE"; record installer_sha256 "$SHA"
ok "$INSTALLER_NAME: $(human "$SIZE"), SHA-256 $SHA"

if [ "$INSTALL_TEST" = 1 ]; then
    step "Install test on this Mac (the final installer)"
    install_test "$PKG"
    [ "$(sha256_of "$PKG")" = "$SHA" ] || die "the installer changed during the install test"
fi

# ============================================================================================
step "Release files"
awk -v v="## $DISPLAY_VERSION" '$0 ~ "^## " { on = (index($0, v) == 1) } on' "$ROOT/$CHANGELOG_FILE" > "$OUT_DIR/RELEASE_NOTES.md"
for doc in INSTALL_GUIDE.md BETA_TESTING.md KNOWN_ISSUES.md; do
    [ -f "$ROOT/docs/release/$doc" ] && cp "$ROOT/docs/release/$doc" "$OUT_DIR/$doc"
done
finish_record
python3 "$RELEASE_DIR/lib/write_release_files.py" "$RECORD_FILE" "$OUT_DIR/release-manifest.json" "$OUT_DIR/VERIFICATION_REPORT.md"
mv "$OUT_DIR" "$FINAL_OUT_DIR"
OUT_DIR="$FINAL_OUT_DIR"
LOG_FILE="$OUT_DIR/logs/release.log"
RECORD_FILE="$OUT_DIR/logs/release-record.txt"

step "Done"
info "Installer:  $OUT_DIR/$INSTALLER_NAME"
info "SHA-256:    $SHA"
info "Manifest:   $OUT_DIR/release-manifest.json"
info "Report:     $OUT_DIR/VERIFICATION_REPORT.md"
if [ ${#NOT_READY[@]} -gt 0 ]; then
    printf '\n%sNOT RELEASE-READY:%s %s\n' "$c_red$c_bold" "$c_off" "$(printf '%s, ' "${NOT_READY[@]}" | sed 's/, $//')"
fi
