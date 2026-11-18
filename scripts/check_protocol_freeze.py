#!/usr/bin/env python3
"""Fail when a frozen protocol stack changed without its version.lock being resealed.

    python3 scripts/check_protocol_freeze.py
    python3 scripts/check_protocol_freeze.py --reseal adsl

A stack is frozen once firmware/protocols/<stack>/version.lock exists, and a stack at 1.0.0 or
later must have one. The lock names the
stack's version and the roots its conformance rests on, files or directories from the
repository root, and holds the SHA-256 of every file under them. The protocols/common/ files the
frozen files include are frozen too, followed include by include, each header with its .cpp
beside it. The check fails when a file under the roots changed, appeared or went, and when the
lock's version is not the kVersion in the stack's version.h.

--reseal rewrites the digests of the roots the lock names, under the current kVersion. Run it after moving kVersion, or after
a change that leaves what the stack puts on air and reads off it as it was, and say which in the
pull request: the lock's diff is what review sees (firmware/protocols/README.md, Versions).
"""
import hashlib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROTOCOLS = "firmware/protocols"
INCLUDE = re.compile(r'^\s*#\s*include\s+"(protocols/common/[^"]+)"', re.M)
VERSION = re.compile(r'kVersion\[\]\s*=\s*"([^"]+)"')


def main(argv):
    if argv[:1] == ["--reseal"] and len(argv) == 2:
        write_lock(argv[1])
        report(f"resealed {lock_path(argv[1])} at {current_version(argv[1])}")
        return 0
    if argv:
        report(__doc__)
        return 2
    problems = unlocked_stacks()
    for stack in frozen_stacks():
        problems += check(stack)
    for problem in problems:
        report(problem)
    if problems:
        report("A frozen stack changed: move its kVersion or reseal, as the docstring says.")
        return 1
    report(f"OK: {', '.join(frozen_stacks()) or 'no stack'} matches its version.lock.")
    return 0


def frozen_stacks(root=ROOT):
    base = os.path.join(root, PROTOCOLS)
    return sorted(name for name in os.listdir(base)
                  if os.path.isfile(os.path.join(base, name, "version.lock")))


def unlocked_stacks(root=ROOT):
    base = os.path.join(root, PROTOCOLS)
    problems = []
    for name in sorted(os.listdir(base)):
        if not os.path.isfile(os.path.join(base, name, "version.h")):
            continue
        version = current_version(name, root)
        if int(version.split(".")[0]) >= 1 and name not in frozen_stacks(root):
            problems.append(f"{name}: version {version} is a release, so it needs a version.lock")
    return problems


def check(stack, root=ROOT):
    version, roots, sealed = read_lock(stack, root)
    problems = []
    if version != current_version(stack, root):
        problems.append(f"{stack}: version.lock says {version}, version.h says "
                        f"{current_version(stack, root)}")
    now = digests(roots, root)
    for path in sorted(set(sealed) | set(now)):
        if path not in now:
            problems.append(f"{stack}: {path} is sealed but gone")
        elif path not in sealed:
            problems.append(f"{stack}: {path} is under a frozen root but not sealed")
        elif sealed[path] != now[path]:
            problems.append(f"{stack}: {path} changed since the seal")
    return problems


def write_lock(stack, root=ROOT):
    _, roots, _ = read_lock(stack, root)
    lines = [f"version {current_version(stack, root)}"]
    lines += [f"root {each}" for each in roots]
    lines += [f"{digest}  {path}" for path, digest in sorted(digests(roots, root).items())]
    with open(lock_path(stack, root), "w", encoding="utf-8") as out:
        out.write("\n".join(lines) + "\n")


def read_lock(stack, root=ROOT):
    version, roots, sealed = None, [], {}
    with open(lock_path(stack, root), encoding="utf-8") as lock:
        for line in lock.read().splitlines():
            if line.startswith("version "):
                version = line.split(" ", 1)[1]
            elif line.startswith("root "):
                roots.append(line.split(" ", 1)[1])
            elif line:
                digest, path = line.split("  ", 1)
                sealed[path] = digest
    return version, roots, sealed


def digests(roots, root=ROOT):
    files = set()
    for each in roots:
        files |= files_under(each, root)
    files |= common_included_by(files, root)
    files = {path for path in files if not path.endswith("/version.lock")}
    return {path: sha256(os.path.join(root, path)) for path in files}


def files_under(each, root):
    full = os.path.join(root, each)
    if os.path.isfile(full):
        return {each}
    if not os.path.isdir(full):
        raise SystemExit(f"a frozen root is neither a file nor a directory: {each}")
    found = set()
    for folder, folders, names in os.walk(full):
        folders[:] = sorted(name for name in folders if name != "__pycache__")
        for name in names:
            found.add(os.path.relpath(os.path.join(folder, name), root).replace(os.sep, "/"))
    return found


def common_included_by(files, root):
    found, pending = set(), list(files)
    while pending:
        with open(os.path.join(root, pending.pop()), encoding="utf-8", errors="replace") as source:
            text = source.read()
        for header in INCLUDE.findall(text):
            for path in (f"firmware/{header}", f"firmware/{os.path.splitext(header)[0]}.cpp"):
                if path not in found and os.path.isfile(os.path.join(root, path)):
                    found.add(path)
                    pending.append(path)
    return found


def current_version(stack, root=ROOT):
    with open(os.path.join(root, PROTOCOLS, stack, "version.h"), encoding="utf-8") as header:
        return VERSION.search(header.read()).group(1)


def lock_path(stack, root=ROOT):
    return os.path.join(root, PROTOCOLS, stack, "version.lock")


def sha256(path):
    with open(path, "rb") as source:
        return hashlib.sha256(source.read()).hexdigest()


def report(message):
    print(message, file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
