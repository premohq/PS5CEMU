#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fetch the pinned inputs listed in tools/deps.json.

    deps.py fetch     fetch whatever is missing (nothing that exists is changed)
    deps.py status    show each item, where it lives and whether it matches its pin
    deps.py clean     remove what fetch created under .deps/

Archives are checked against their SHA-256 before use and cached in
~/.cache/ps5cemu-deps (PS5CEMU_DEPS_CACHE). Git items are fetched at their pinned
commit, with only the submodules the item names.
"""

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.environ.get("PS5CEMU_DEPS_CACHE", os.path.join(os.path.expanduser("~"), ".cache", "ps5cemu-deps"))


def load_items():
    with open(os.path.join(ROOT, "tools", "deps.json"), encoding="utf-8") as f:
        return json.load(f)["items"]


def here(path):
    return os.path.join(ROOT, path)


def run(command, cwd=None):
    subprocess.run(command, cwd=cwd, check=True)


def git(path, *arguments, capture=False):
    result = subprocess.run(["git", "-C", path, *arguments], check=not capture, capture_output=capture, text=True)
    return result.stdout.strip() if capture else None


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def download(item):
    os.makedirs(CACHE, exist_ok=True)
    target = os.path.join(CACHE, os.path.basename(item["url"]))
    if os.path.isfile(target) and sha256(target) == item["sha256"]:
        return target
    print(f"==> [deps] downloading {item['name']}: {item['url']}", flush=True)
    partial = target + ".download"
    with urllib.request.urlopen(item["url"]) as response, open(partial, "wb") as out:
        shutil.copyfileobj(response, out)
    actual = sha256(partial)
    if actual != item["sha256"]:
        os.remove(partial)
        sys.exit(f"{item['name']}: SHA-256 mismatch, expected {item['sha256']}, got {actual}")
    os.replace(partial, target)
    return target


def fetch_archive(item):
    archive = download(item)
    destination = here(item["extract"])
    os.makedirs(destination, exist_ok=True)
    print(f"==> [deps] extracting {item['name']} into {item['extract']}", flush=True)
    with tarfile.open(archive) as tar:
        tar.extractall(destination, filter="tar") if sys.version_info >= (3, 12) else tar.extractall(destination)


def fetch_files(item):
    destination = here(item["dest"])
    os.makedirs(destination, exist_ok=True)
    for entry in item["files"]:
        source = download({"name": f"{item['name']}/{entry['name']}", **entry})
        shutil.copyfile(source, os.path.join(destination, entry["name"]))


def fetch_git(item):
    path = here(item["path"])
    if not os.path.isdir(os.path.join(path, ".git")):
        print(f"==> [deps] fetching {item['name']} at {item['commit'][:12]}", flush=True)
        os.makedirs(path, exist_ok=True)
        git(path, "init", "-q")
        git(path, "remote", "add", "origin", item["url"])
        # GitHub serves any reachable commit by its id, so a shallow fetch is enough.
        if subprocess.run(["git", "-C", path, "fetch", "-q", "--depth", "1", "origin", item["commit"]]).returncode != 0:
            git(path, "fetch", "-q", "origin")
        git(path, "checkout", "-q", "--detach", item["commit"])
    for submodule in item.get("submodules", []):
        checkout = os.path.join(path, submodule)
        if not os.path.isdir(checkout) or not os.listdir(checkout):
            print(f"==> [deps] fetching {item['name']} submodule {submodule}", flush=True)
            git(path, "submodule", "update", "--init", "--depth", "1", "--", submodule)
    if "setup" in item and not os.path.exists(here(item["setup_creates"])):
        print(f"==> [deps] setting up {item['name']}", flush=True)
        run(item["setup"], cwd=path)


def created(item):
    marker = item.get("creates") or item.get("setup_creates")
    return marker is not None and os.path.exists(here(marker)) and (
        "setup_creates" not in item or os.path.exists(here(item["setup_creates"])))


def fetch(items):
    for item in items:
        if created(item):
            continue
        if item["kind"] == "archive":
            fetch_archive(item)
        elif item["kind"] == "files":
            fetch_files(item)
        else:
            fetch_git(item)
        if not created(item):
            sys.exit(f"{item['name']}: fetched, but {item.get('creates') or item.get('setup_creates')} is missing")


def status(items):
    for item in items:
        if item["kind"] == "git":
            path = here(item["path"])
            head = git(path, "rev-parse", "HEAD", capture=True) if os.path.isdir(os.path.join(path, ".git")) else ""
            state = "missing" if not head else ("ok" if head == item["commit"] else f"at {head[:12]}, pin {item['commit'][:12]}")
            print(f"{item['name']:14} {item['path']:40} {state}")
        else:
            print(f"{item['name']:14} {item.get('extract') or item['dest']:40} {'ok' if created(item) else 'missing'}")


def clean(items):
    for item in items:
        path = item.get("path") or item["creates"].split("/")[0] + "/" + item["creates"].split("/")[1]
        if path.startswith(".deps/") and os.path.exists(here(path)):
            print(f"rm -rf {path}")
            shutil.rmtree(here(path))


def main():
    command = sys.argv[1] if len(sys.argv) > 1 else "fetch"
    items = load_items()
    if command == "fetch":
        fetch(items)
    elif command == "status":
        status(items)
    elif command == "clean":
        clean(items)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
