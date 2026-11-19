#!/usr/bin/env bash
# The installer: one component package per format (selectable), an optional payload-free
# package that moves earlier per-user test builds aside, and a signed product archive.

# html_template <in> <out>: fills the product's placeholders.
fill_template() {
    sed -e "s|@PRODUCT_NAME@|$PRODUCT_NAME|g" \
        -e "s|@PRODUCT_FILE_STEM@|$PRODUCT_FILE_STEM|g" \
        -e "s|@DISPLAY_VERSION@|$DISPLAY_VERSION|g" \
        -e "s|@MIN_MACOS@|$MIN_MACOS|g" \
        -e "s|@CHANNEL_LINE@|$CHANNEL_LINE|g" \
        -e "s|@LEGACY_PATHS@|$LEGACY_PATHS_QUOTED|g" \
        "$1" > "$2"
}

# component_package <choice id> <staged bundle dir> <install location> <identifier> <out.pkg>
component_package() {
    local choice="$1" root="$2" location="$3" identifier="$4" out="$5"
    local plist="$WORK/$choice-components.plist" scripts=""
    # Not relocatable: macOS would otherwise "update" a copy it finds elsewhere (an older test
    # build in ~/Applications, a build folder) instead of installing at $location.
    run pkgbuild --analyze --root "$root" "$plist"
    local i=0
    while "$PLISTBUDDY" -c "Print :$i" "$plist" > /dev/null 2>&1; do
        run "$PLISTBUDDY" -c "Set :$i:BundleIsRelocatable false" "$plist"
        run "$PLISTBUDDY" -c "Set :$i:BundleIsVersionChecked false" "$plist"
        i=$((i + 1))
    done
    [ "$i" -gt 0 ] || die "$choice: pkgbuild found no bundle in $root"
    if [ "$choice" = "au" ]; then
        scripts="$WORK/scripts-au"
        mkdir -p "$scripts"
        cp "$RELEASE_DIR/templates/scripts/au-postinstall" "$scripts/postinstall"
        chmod 755 "$scripts/postinstall"
    fi
    run pkgbuild --root "$root" --component-plist "$plist" --install-location "$location" \
        --identifier "$identifier" --version "$PKG_VERSION" ${scripts:+--scripts "$scripts"} "$out"
}

legacy_package() {
    local out="$1" scripts="$WORK/scripts-legacy"
    mkdir -p "$scripts"
    fill_template "$RELEASE_DIR/templates/scripts/legacy-postinstall.in" "$scripts/postinstall"
    chmod 755 "$scripts/postinstall"
    bash -n "$scripts/postinstall" || die "legacy postinstall: syntax error"
    run pkgbuild --nopayload --scripts "$scripts" --identifier "$LEGACY_PACKAGE_ID" --version "$PKG_VERSION" "$out"
}

# distribution.xml: the installer's title, pages, requirements and the choices (every format
# ticked, the test-build move unticked).
write_distribution() {
    local out="$1" line choice bundle location identifier title ent description
    {
        echo '<?xml version="1.0" encoding="utf-8"?>'
        echo '<installer-gui-script minSpecVersion="2">'
        echo "    <title>$PRODUCT_NAME $DISPLAY_VERSION</title>"
        echo '    <welcome file="welcome.html" mime-type="text/html"/>'
        echo '    <conclusion file="conclusion.html" mime-type="text/html"/>'
        echo '    <options customize="allow" require-scripts="false" rootVolumeOnly="true" hostArchitectures="arm64,x86_64"/>'
        echo '    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>'
        echo "    <allowed-os-versions><os-version min=\"$MIN_MACOS\"/></allowed-os-versions>"
        echo '    <choices-outline>'
        for line in "${COMPONENTS[@]}"; do
            echo "        <line choice=\"${line%%|*}\"/>"
        done
        echo '        <line choice="legacy"/>'
        echo '    </choices-outline>'
        for line in "${COMPONENTS[@]}"; do
            IFS='|' read -r choice bundle location identifier title ent description <<< "$line"
            echo "    <choice id=\"$choice\" title=\"$title\" description=\"$description\" start_selected=\"true\" visible=\"true\">"
            echo "        <pkg-ref id=\"$identifier\"/>"
            echo '    </choice>'
            echo "    <pkg-ref id=\"$identifier\" version=\"$PKG_VERSION\" onConclusion=\"none\">$choice.pkg</pkg-ref>"
        done
        echo "    <choice id=\"legacy\" title=\"Move earlier OSP test builds aside\" description=\"Moves OSP test builds from your own ~/Library and ~/Applications folders to ~/Library/Application Support/ANDOR/Previous Test Builds, so your DAW finds only this version. Nothing is deleted.\" start_selected=\"false\" visible=\"true\">"
        echo "        <pkg-ref id=\"$LEGACY_PACKAGE_ID\"/>"
        echo '    </choice>'
        echo "    <pkg-ref id=\"$LEGACY_PACKAGE_ID\" version=\"$PKG_VERSION\" onConclusion=\"none\">legacy.pkg</pkg-ref>"
        echo '</installer-gui-script>'
    } > "$out"
    if command -v xmllint > /dev/null; then
        xmllint --noout "$out" || die "distribution.xml is not well-formed"
    fi
}

build_installer() {
    local out="$1" line choice bundle location identifier title ent description
    local pkgs="$WORK/pkgs" resources="$WORK/resources"
    rm -rf "$pkgs" "$resources"
    mkdir -p "$pkgs" "$resources"
    for line in "${COMPONENTS[@]}"; do
        IFS='|' read -r choice bundle location identifier title ent description <<< "$line"
        component_package "$choice" "$STAGE/$choice" "$location" "$identifier" "$pkgs/$choice.pkg"
    done
    legacy_package "$pkgs/legacy.pkg"
    fill_template "$ROOT/$INSTALLER_RESOURCES/welcome.html" "$resources/welcome.html"
    fill_template "$ROOT/$INSTALLER_RESOURCES/conclusion.html" "$resources/conclusion.html"
    write_distribution "$WORK/distribution.xml"
    cp "$WORK/distribution.xml" "$OUT_DIR/logs/distribution.xml"

    local -a sign=(--sign "$INSTALLER_IDENTITY" --timestamp)
    if [ -n "${SIGN_KEYCHAIN:-}" ]; then
        sign+=(--keychain "$SIGN_KEYCHAIN")
    fi
    run productbuild --distribution "$WORK/distribution.xml" --resources "$resources" \
        --package-path "$pkgs" "${sign[@]}" "$out"
}
