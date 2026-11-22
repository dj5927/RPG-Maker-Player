"""Safely publish verified SteamOS v1.7.1 artifacts to a DRAFT GitHub release.

Never publish a partial release. No older release is modified, and remote assets
that do not match the pre-verified local digest are not replaced automatically.
"""
from __future__ import annotations
import hashlib
import http.client
import json
from pathlib import Path
import sys
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(r"D:\GPT\MKXP")
sys.path.insert(0, str(Path(r"D:\GPT\MKXP_ANDROID\work")))
from publish_android_rc4 import token_from_credential_manager

API = "https://api.github.com/repos/dj5927/RPG-Maker-Player"
TAG = "v1.7.1"
NAMES = (
    "RPG_Maker_Player_SteamOS_V1.7.1.tar.gz",
    "RPG_Maker_Player_SteamOS_V1.7.1.tar.gz.sha256",
    "RPG_Maker_Player_SteamOS_Update_V1.7.1.tar.gz",
    "RPG_Maker_Player_SteamOS_Update_V1.7.1.tar.gz.sha256",
)
NOTES = ROOT / "work/STEAMOS_20261122/docs/RELEASE_NOTES_STEAMOS_V171.md"


def digest(file):
    with file.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def http_json(token, method, url, obj=None):
    data = None if obj is None else json.dumps(obj, ensure_ascii=False).encode("utf-8")
    headers = {
        "Authorization": "Bearer " + token,
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "RPG-Maker-Player-SteamOS-1.7.1",
    }
    if data is not None:
        headers["Content-Type"] = "application/json"
    request = urllib.request.Request(url, data=data, headers=headers, method=method)
    with urllib.request.urlopen(request, timeout=100) as response:
        return json.load(response)


def local_files():
    output = {}
    for name in NAMES:
        file = ROOT / "DIST" / name
        assert file.is_file(), "missing release asset " + name
        output[name] = (file, file.stat().st_size, digest(file))
        print("LOCAL", name, output[name][1], output[name][2], flush=True)
    for base in (NAMES[0], NAMES[2]):
        assert (ROOT / "DIST" / (base + ".sha256")).read_text().split()[0] == output[base][2]
    return output


def ensure_tag_matches(token, expected_commit):
    source = http_json(token, "GET", API + "/commits/" + TAG)
    assert source["sha"] == expected_commit, (
        "refusing release: tag v1.7.1 does not point to tested source",
        source["sha"], expected_commit)
    print("TAG_MATCHES_COMMIT", expected_commit, flush=True)


def ensure_draft(token, notes, commit):
    # A draft release is often hidden from /releases/tags/{tag}. The full
    # authenticated release list is the authoritative resume source.
    try:
        result = http_json(token, "GET", API + "/releases/tags/" + TAG)
    except urllib.error.HTTPError as error:
        if error.code != 404:
            raise
        listed = http_json(token, "GET", API + "/releases?per_page=100")
        matches = [item for item in listed if item["tag_name"] == TAG]
        if matches:
            assert len(matches) == 1, "multiple matching release drafts"
            result = matches[0]
        else:
            result = http_json(token, "POST", API + "/releases", {
                "tag_name": TAG,
                "target_commitish": commit,
                "name": "RPG Maker Player SteamOS 1.7.1",
                "body": notes,
                "draft": True,
                "prerelease": False,
                "generate_release_notes": False,
            })
    assert result["tag_name"] == TAG, result.get("tag_name")
    if not result["draft"]:
        raise RuntimeError("release already public; refusing to modify assets")
    print("DRAFT_RELEASE_READY", result["id"], flush=True)
    return result

def upload(token, release, name, file, length, checksum):
    base_url = release["upload_url"].split("{", 1)[0]
    parsed = urllib.parse.urlsplit(base_url)
    assert parsed.scheme == "https" and parsed.hostname == "uploads.github.com"
    target = parsed.path + "?" + urllib.parse.urlencode({"name": name})
    connection = http.client.HTTPSConnection(parsed.hostname, timeout=1200)
    try:
        connection.putrequest("POST", target)
        for key, value in (
            ("Authorization", "Bearer " + token),
            ("Accept", "application/vnd.github+json"),
            ("X-GitHub-Api-Version", "2022-11-28"),
            ("User-Agent", "RPG-Maker-Player-SteamOS-1.7.1"),
            ("Content-Type", "application/octet-stream"),
            ("Content-Length", str(length)),
        ):
            connection.putheader(key, value)
        connection.endheaders()
        sent = 0
        with file.open("rb") as src:
            while (part := src.read(4 * 1024 * 1024)):
                connection.send(part)
                sent += len(part)
                if sent // (64 * 1024 * 1024) != (sent - len(part)) // (64 * 1024 * 1024):
                    print("UPLOAD_PROGRESS", name, sent, "/", length, flush=True)
        assert sent == length
        response = connection.getresponse()
        body = response.read()
        if response.status != 201:
            raise RuntimeError(
                "GitHub upload error " + str(response.status) + ": " +
                body.decode("utf-8", "replace")[:900])
        remote = json.loads(body)
        assert remote["name"] == name and remote["size"] == length
        if remote.get("digest"):
            assert remote["digest"].casefold() == "sha256:" + checksum
        print("UPLOAD_OK", name, remote["size"], remote.get("digest"), flush=True)
    finally:
        connection.close()


def publish():
    files = local_files()
    token = token_from_credential_manager()
    commit = (ROOT / "work/v171_release_source_sha.txt").read_text().strip()
    assert len(commit) == 40
    ensure_tag_matches(token, commit)
    notes = NOTES.read_text(encoding="utf-8")
    draft = ensure_draft(token, notes, commit)
    for name, (file, length, checksum) in files.items():
        current = http_json(token, "GET", API + "/releases/" + str(draft["id"]))
        listed = {x["name"]: x for x in current["assets"]}
        if name in listed:
            remote = listed[name]
            assert remote["state"] == "uploaded" and remote["size"] == length
            assert remote.get("digest", "").casefold() == "sha256:" + checksum, (
                "remote asset mismatch; do not overwrite", name)
            print("EXISTING_VERIFIED", name, flush=True)
        else:
            upload(token, current, name, file, length, checksum)

    before = http_json(token, "GET", API + "/releases/" + str(draft["id"]))
    assets = {x["name"]: x for x in before["assets"]}
    assert set(assets) == set(NAMES), list(assets)
    for name, (_, length, checksum) in files.items():
        item = assets[name]
        assert item["size"] == length and item["state"] == "uploaded"
        assert item.get("digest", "").casefold() == "sha256:" + checksum
    print("ALL_FOUR_ASSETS_REMOTE_SHA256_VERIFIED", flush=True)

    final = http_json(token, "PATCH", API + "/releases/" + str(draft["id"]), {
        "name": "RPG Maker Player SteamOS 1.7.1",
        "body": notes,
        "draft": False,
        "prerelease": False,
    })
    assert not final["draft"] and not final["prerelease"]
    latest = http_json(token, "GET", API + "/releases/latest")
    assert latest["tag_name"] == TAG, ("OTA latest points to", latest["tag_name"])
    print("STEAMOS_V17_OFFICIAL_RELEASE_PUBLISHED", final["html_url"], flush=True)
    for name in NAMES:
        print("PUBLIC_ASSET", name, assets[name]["browser_download_url"], flush=True)


if __name__ == "__main__":
    publish()
