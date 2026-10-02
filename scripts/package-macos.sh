#!/usr/bin/env bash
# Builds a signed, notarised macOS installer for OSP (AU + VST3 + Standalone).
#
# NOT RUN IN CI: it needs an Apple Developer ID. Before the first real use, check every
# step on a Mac. Required environment:
#   OSP_APP_IDENTITY        "Developer ID Application: <Name> (<TEAMID>)"
#   OSP_INSTALLER_IDENTITY  "Developer ID Installer: <Name> (<TEAMID>)"
#   OSP_NOTARY_PROFILE      keychain profile created with `xcrun notarytool store-credentials`
# Optional: OSP_VERSION (default: from CMakeLists.txt), OSP_BUILD_DIR (default build-release).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${OSP_BUILD_DIR:-$root/build-release}"
version="${OSP_VERSION:-$(sed -n 's/^    VERSION \([0-9.]*\)$/\1/p' "$root/CMakeLists.txt" | head -1)}"
: "${OSP_APP_IDENTITY:?set OSP_APP_IDENTITY}"
: "${OSP_INSTALLER_IDENTITY:?set OSP_INSTALLER_IDENTITY}"
: "${OSP_NOTARY_PROFILE:?set OSP_NOTARY_PROFILE}"

cmake -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DOSP_BUILD_PLUGIN=ON \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build "$build"
ctest --test-dir "$build" --output-on-failure

artefacts="$build/apps/plugin/OSP_Plugin_artefacts/Release"
stage="$build/package"
rm -rf "$stage" && mkdir -p "$stage"/{au,vst3,app}
cp -R "$artefacts/AU/OSP.component" "$stage/au/"
cp -R "$artefacts/VST3/OSP.vst3" "$stage/vst3/"
cp -R "$artefacts/Standalone/OSP.app" "$stage/app/"

for bundle in "$stage/au/OSP.component" "$stage/vst3/OSP.vst3" "$stage/app/OSP.app"; do
    codesign --force --deep --options runtime --timestamp --sign "$OSP_APP_IDENTITY" "$bundle"
done

pkgbuild --root "$stage/au" --install-location "/Library/Audio/Plug-Ins/Components" \
         --identifier com.osp.instrument.au --version "$version" "$stage/osp-au.pkg"
pkgbuild --root "$stage/vst3" --install-location "/Library/Audio/Plug-Ins/VST3" \
         --identifier com.osp.instrument.vst3 --version "$version" "$stage/osp-vst3.pkg"
pkgbuild --root "$stage/app" --install-location "/Applications" \
         --identifier com.osp.instrument.app --version "$version" "$stage/osp-app.pkg"
productbuild --sign "$OSP_INSTALLER_IDENTITY" \
             --package "$stage/osp-au.pkg" --package "$stage/osp-vst3.pkg" --package "$stage/osp-app.pkg" \
             "$build/OSP-$version.pkg"

xcrun notarytool submit "$build/OSP-$version.pkg" --keychain-profile "$OSP_NOTARY_PROFILE" --wait
xcrun stapler staple "$build/OSP-$version.pkg"
echo "Installer: $build/OSP-$version.pkg"
