#!/usr/bin/env python3
"""Writes a release's machine-readable manifest and its verification report.

Both come from the release record (key=value lines the release script wrote as each step
passed), so they can only claim what actually happened. Manifest format: kind
"andor.release-manifest", schemaVersion 1 (docs/release/RELEASE_RUNBOOK.md).

    write_release_files.py <record> <manifest.json> <report.md>
"""
import json
import sys


def read_record(path):
    rec = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if "=" in line:
                k, v = line.split("=", 1)
                rec[k] = v
    return rec


def as_int(v):
    try:
        return int(v)
    except (TypeError, ValueError):
        return None


def manifest(rec):
    components = []
    for cid in rec.get("component_ids", "").split():
        components.append({
            "id": cid,
            "bundle": rec.get(f"component_{cid}_bundle"),
            "installPath": rec.get(f"component_{cid}_install_path"),
            "packageIdentifier": rec.get(f"component_{cid}_package_id"),
            "bytes": as_int(rec.get(f"component_{cid}_size")),
            "cdhash": rec.get(f"component_{cid}_cdhash"),
        })
    return {
        "kind": "andor.release-manifest",
        "schemaVersion": 1,
        "product": {
            "id": rec.get("product_id"),
            "name": rec.get("product_name"),
            "bundleId": rec.get("bundle_id"),
            "audioUnit": {"type": rec.get("au_type"), "subtype": rec.get("au_subtype"),
                          "manufacturer": rec.get("au_manufacturer")},
        },
        "version": {
            "semver": rec.get("display_version"),
            "marketing": rec.get("version"),
            "prerelease": rec.get("prerelease") or None,
            "build": as_int(rec.get("build_number")),
            "bundleVersion": rec.get("bundle_version"),
        },
        # A beta is published to the beta channel only; nothing here offers it to stable.
        "channel": rec.get("channel"),
        "platform": {"os": "macOS", "minimumVersion": rec.get("min_macos"),
                     "architectures": ["arm64", "x86_64"]},
        "source": {"repository": rec.get("repository"), "branch": rec.get("branch"),
                   "commit": rec.get("commit"), "clean": rec.get("tree_clean") == "yes"},
        "build": {"date": rec.get("build_date"), "xcode": rec.get("xcode"),
                  "sdk": rec.get("sdk"), "host": rec.get("build_host_arch"),
                  "factoryAuthoring": False, "testsRun": rec.get("tests"),
                  "factoryPacks": rec.get("factory_packs") or "none"},
        "publication": {"date": None, "url": None},   # filled in when the owner publishes
        "artifacts": [{
            "type": "pkg",
            "filename": rec.get("installer_name"),
            "bytes": as_int(rec.get("installer_size")),
            "sha256": rec.get("installer_sha256"),
            "signed": {"identity": "Developer ID Installer", "teamId": rec.get("team_id")},
            "notarization": {"submissionId": rec.get("notary_submission_id"),
                             "status": rec.get("notary_status"),
                             "stapled": rec.get("stapled") == "yes"},
        }],
        "components": components,
        "changelog": rec.get("changelog"),
        "releaseReady": rec.get("release_ready") == "yes",
    }


def report(rec, man):
    def line(label, key, missing="not run"):
        return f"| {label} | {rec.get(key, missing)} |"

    art = man["artifacts"][0]
    out = [
        f"# Verification report: {rec.get('product_name')} {rec.get('display_version')}",
        "",
        "Written by packaging/release/andor-release.sh from what each step recorded; a step",
        "that did not run says so.",
        "",
        "## Release",
        "",
        "| | |",
        "|---|---|",
        f"| Installer | `{art['filename']}` |",
        f"| Size | {art['bytes']} bytes |",
        f"| SHA-256 | `{art['sha256']}` |",
        f"| Version | {rec.get('display_version')} (CFBundleVersion {rec.get('bundle_version')}, channel {rec.get('channel')}) |",
        f"| Source | {rec.get('repository')} `{rec.get('branch')}` @ `{rec.get('commit')}` (clean tree: {rec.get('tree_clean')}) |",
        f"| Built | {rec.get('build_date')}, Xcode {rec.get('xcode')}, SDK {rec.get('sdk')}, on {rec.get('build_host_arch')} |",
        f"| Release-ready | **{'yes' if man['releaseReady'] else 'NO'}** {rec.get('release_ready_reason', '')} |",
        "",
        "## Automated checks",
        "",
        "| Check | Result |",
        "|---|---|",
        line("Customer configuration (Release, OSP_FACTORY_AUTHORING OFF)", "config_check"),
        line("Regression suite (ctest)", "tests"),
        line("Universal binaries, deployment target", "architectures"),
        line("Bundle identity and versions", "identity_check"),
        line("Code signatures (each bundle)", "bundle_signatures"),
        line("Installer signature", "installer_signature"),
        line("Payload = signed bundles, no forbidden files", "payload_check"),
        line("Notarization", "notary_status"),
        line("Submission ID", "notary_submission_id", "-"),
        line("Stapled ticket", "stapled"),
        line("Gatekeeper (installer)", "gatekeeper"),
        line("Installed on this Mac", "install_test"),
        line("Duplicate per-user copies", "duplicates"),
        line("auval", "auval"),
        line("pluginval VST3", "pluginval_vst3"),
        line("pluginval AU", "pluginval_au"),
        line("Standalone launch", "standalone_launch"),
        "",
        "## Components",
        "",
        "| Component | Installs to | Size | CDHash |",
        "|---|---|---|---|",
    ]
    for c in man["components"]:
        out.append(f"| {c['bundle']} | {c['installPath']} | {c['bytes']} bytes | `{c['cdhash']}` |")
    warnings = [w for w in rec.get("warnings", "").split(";") if w]
    out += ["", "## Warnings", ""]
    out += [f"- {w}" for w in warnings] or ["None."]
    out += [
        "",
        "## Not tested by this script",
        "",
        "Manual host and hardware checks are recorded in docs/release/BETA_TESTING.md (owner",
        "checklist). Nothing below is claimed by this report: Logic, Ableton Live, REAPER and",
        "other hosts; the architecture this Mac is not; other macOS versions; upgrade from",
        "earlier test builds; offline use; microphone recording.",
        "",
    ]
    return "\n".join(out)


def main():
    rec = read_record(sys.argv[1])
    man = manifest(rec)
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        json.dump(man, f, indent=2)
        f.write("\n")
    with open(sys.argv[3], "w", encoding="utf-8") as f:
        f.write(report(rec, man))


if __name__ == "__main__":
    main()
