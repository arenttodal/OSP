# Owner setup: signing and notarization for ANDOR releases

What the release command (`packaging/release/andor-release.sh`) needs, on the owner's Mac and
on GitHub. Nothing secret is ever in this repository: certificates and private keys live in a
keychain, notarization credentials in a notarytool keychain profile or GitHub secrets.

## On the Mac (local releases)

Done already (19 Nov 2026); kept here for a new Mac.

| Need | How | Check |
|---|---|---|
| Xcode 16.4 or later, its command-line tools | App Store; `sudo xcode-select -s /Applications/Xcode.app` | `xcodebuild -version` |
| CMake 3.25+, Ninja | `brew install cmake ninja` | `cmake --version`, `ninja --version` |
| GitHub CLI (only when `packs.lock` pins factory packs) | `brew install gh`, `gh auth login` | `gh auth status` |
| Developer ID Application certificate, team J2MAYE3B89 | Xcode › Settings › Accounts › Manage Certificates, or developer.apple.com | `security find-identity -v -p codesigning` lists "Developer ID Application: … (J2MAYE3B89)" |
| Developer ID Installer certificate, same team | the same | `security find-identity -v` lists "Developer ID Installer: … (J2MAYE3B89)" |
| Notarization profile `ANDOR-notary` | `xcrun notarytool store-credentials ANDOR-notary` (Apple ID + app-specific password, or an App Store Connect API key) | `xcrun notarytool history --keychain-profile ANDOR-notary` |

The script finds both certificates by the team ID in `packaging/release/signing.conf`; no
certificate name is written in the repository. With two matching certificates (for example an
expired one still in the keychain), name the one to use:

```sh
export ANDOR_APP_IDENTITY="Developer ID Application: <name> (J2MAYE3B89)"
export ANDOR_INSTALLER_IDENTITY="Developer ID Installer: <name> (J2MAYE3B89)"
```

The first signature may ask to let `codesign` use the key: choose **Always Allow**.

## GitHub Actions (automated releases; not needed for the first beta)

The workflow `.github/workflows/release-macos.yml` signs and notarizes on GitHub's Macs. The
local `ANDOR-notary` profile does not exist there, so the runner gets its own credentials.

### 1. Export the two certificates

In **Keychain Access › login › My Certificates**, select "Developer ID Application: …
(J2MAYE3B89)" (with its private key), **File › Export Items…**, format `.p12`, a strong
password. Do the same for "Developer ID Installer: …". Use the same password for both. Then:

```sh
base64 -i DeveloperID-Application.p12 | pbcopy   # paste as MACOS_DEVELOPER_ID_APPLICATION_P12
base64 -i DeveloperID-Installer.p12 | pbcopy     # paste as MACOS_DEVELOPER_ID_INSTALLER_P12
```

Delete the `.p12` files afterwards (`rm -P`); never commit or email them.

### 2. An App Store Connect API key for notarization

App Store Connect › Users and Access › Integrations › **Team Keys** › **+**, role
**Developer**. Download the `.p8` (possible once), note its **Key ID** and the **Issuer ID** at
the top of the page.

```sh
base64 -i AuthKey_XXXXXXXXXX.p8 | pbcopy        # paste as NOTARY_API_KEY_P8
```

An API key, not an Apple ID password: it can be revoked on its own, and it does not need
two-factor prompts.

### 3. Two environments and six secrets

GitHub › the OSP repository › **Settings › Environments**:

1. **New environment** `release-signing`. Under **Deployment protection rules**, tick
   **Required reviewers** and add yourself: every signing run waits for your approval before
   any secret reaches the runner. Under **Deployment branches and tags**, choose
   **Selected branches and tags** and allow the branches releases are made from.
2. In `release-signing`, **Environment secrets › Add secret**, six times:

| Secret | Value |
|---|---|
| `MACOS_DEVELOPER_ID_APPLICATION_P12` | base64 of the Application `.p12` |
| `MACOS_DEVELOPER_ID_INSTALLER_P12` | base64 of the Installer `.p12` |
| `MACOS_CERTIFICATES_P12_PASSWORD` | the `.p12` export password |
| `NOTARY_API_KEY_P8` | base64 of the `.p8` |
| `NOTARY_API_KEY_ID` | the Key ID |
| `NOTARY_API_ISSUER_ID` | the Issuer ID |

3. **New environment** `release-publish`, also with you as required reviewer, and no secrets.
   It guards the optional draft pre-release.

Secrets belong to the environment, not the repository, so ordinary CI (`ci.yml`) and pull
requests never see them. The workflow decodes them into a temporary keychain that it deletes
when the run ends, success or not.

### Rotating or revoking

- **A certificate expires or leaks:** revoke it on developer.apple.com (Certificates), make a
  new one, replace the two `.p12` secrets. Installers already notarized keep working.
- **The API key leaks:** revoke it in App Store Connect, make a new one, replace the three
  `NOTARY_API_*` secrets.
- **The local profile:** `xcrun notarytool store-credentials ANDOR-notary` again.
