#!/usr/bin/env python3
"""Check that the macOS shell in mac/ has kept up with the Windows side.

    python3 scripts/check-parity.py            report
    python3 scripts/check-parity.py --fix      also repair the mechanical drift

Both halves of this repository -- ime/ (TSF) and mac/ (InputMethodKit) --
are shells over the same mspy::Composer. Nothing detects a shell that has
fallen behind, because the Mac half compiles only in CI and behaves only on
a Mac. These are the checks that can be made mechanically:

  1  mac/Makefile's source lists against CMakeLists.txt   (auto-fixable)
  2  ArtBridge.mm's coverage of composer.h's public surface
  3  upstream commits since the last recorded alignment
  4  one VERSION, agreed everywhere                       (auto-fixable)

--fix touches only what is derived rather than authored. It will not edit
ObjC++ behaviour, will not reorder the key routing, and will not advance the
alignment marker: moving that is a human asserting the review happened.

Standard library only, so it runs on both hosts and both CI runners.
"""

import argparse
import hashlib
import pathlib
import re
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
MARKER = REPO / "mac" / "upstream-alignment.txt"
ALLOW = REPO / "mac" / "parity-allow.txt"

# Changes here can require work in mac/src/. docs/spec.md is the behaviour
# authority both shells implement.
WATCHED = ["core/", "engine/", "cli/", "data/", "docs/spec.md"]

# Windows functions the Mac transliterates rather than shares. Hashing the
# body is the only mechanical signal that a transliteration has gone stale.
TRANSLITERATED = {
    "is_virtual_key_need_mspy": (
        "ime/SampleIME/CompositionProcessorEngine.cpp",
        "IsVirtualKeyNeedMspy",
        "mac/src/ArtInputController.mm  -handleKeyDown:client:",
    ),
    "is_virtual_key_need_mspy_english": (
        "ime/SampleIME/CompositionProcessorEngine.cpp",
        "IsVirtualKeyNeedMspyEnglish",
        "mac/src/ArtInputController.mm  -handleEnglishKeyDown:client:shift:",
    ),
}


def read_marker():
    values = {}
    if not MARKER.is_file():
        return values
    for line in MARKER.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if "=" in line:
            key, _, value = line.partition("=")
            values[key.strip()] = value.strip()
    return values


def write_marker(updates):
    out = []
    for line in MARKER.read_text(encoding="utf-8").splitlines(keepends=True):
        bare = line.split("#", 1)[0]
        if "=" in bare:
            key = bare.partition("=")[0].strip()
            if key in updates:
                comment = line[len(bare):]
                if not comment.strip():
                    comment = "\n"
                line = "%-32s= %s%s" % (key, updates[key], comment)
        out.append(line)
    MARKER.write_text("".join(out), encoding="utf-8", newline="")


def git(*args):
    return subprocess.run(
        ["git", "-C", str(REPO), *args],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
    ).stdout.strip()


def function_body_hash(path, name):
    """Hash of a C++ function definition, located by name rather than line.

    Line numbers drift -- the comment in ArtInputController.mm still cites
    1597, which is the call site, while the definition sits at 1600 -- so the
    anchor has to be the name. A rename makes this return None, which is
    reported loudly: that is the case which would otherwise orphan the Mac's
    key routing silently.
    """
    text = (REPO / path).read_text(encoding="utf-8", errors="replace")
    pattern = r"^[\w:<>,\s\*&_()]*?\b" + re.escape(name) + r"\s*\("
    match = re.search(pattern, text, re.M)
    if not match:
        return None
    start = text.index("{", match.end() - 1)
    depth, i = 0, start
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                break
        i += 1
    body = text[start:i + 1]
    # Whitespace-insensitive, so reindenting is not reported as a change.
    flat = re.sub(r"\s+", " ", body)
    return hashlib.sha256(flat.encode("utf-8")).hexdigest()


def cmake_sources():
    text = (REPO / "CMakeLists.txt").read_text(encoding="utf-8")
    out = {}
    for target in ("mcb_engine", "mspy_core"):
        match = re.search(r"add_library\(" + target + r"\s+STATIC(.*?)\n\)",
                          text, re.S)
        out[target] = re.findall(r"(\S+\.cpp)", match.group(1))
    return out


def makefile_sources():
    text = (REPO / "mac" / "Makefile").read_text(encoding="utf-8")
    out = {}
    for var, macro, folder in (("ENGINE_SRC", "$(ENGINE_DIR)/", "engine/"),
                               ("CORE_SRC", "$(CORE_DIR)/", "core/")):
        match = re.search(r"^" + var + r" := \\\n((?:.*\\\n)*.*)$", text, re.M)
        files = []
        for line in match.group(1).split("\n"):
            line = line.strip().rstrip("\\").strip()
            if line.startswith(macro):
                files.append(folder + line[len(macro):])
        out[var] = files
    return out


PAIRS = (("mcb_engine", "ENGINE_SRC", "$(ENGINE_DIR)", "engine/"),
         ("mspy_core", "CORE_SRC", "$(CORE_DIR)", "core/"))


def check_source_lists(problems, fixes, fix):
    cmake = cmake_sources()
    make = makefile_sources()
    stale = []
    for target, var, macro, folder in PAIRS:
        if cmake[target] == make[var]:
            continue
        stale.append((target, var, macro, folder))
        missing = sorted(set(cmake[target]) - set(make[var]))
        extra = sorted(set(make[var]) - set(cmake[target]))
        detail = []
        if missing:
            detail.append("%s is missing %s" % (var, ", ".join(missing)))
        if extra:
            detail.append("%s still lists %s" % (var, ", ".join(extra)))
        if not detail:
            detail.append("%s is in a different order from CMakeLists %s"
                          % (var, target))
        problems.append(
            "mac/Makefile drifted from CMakeLists.txt -- a link error only "
            "the Mac sees:\n      " + "\n      ".join(detail))

    if stale and fix:
        text = (REPO / "mac" / "Makefile").read_text(encoding="utf-8")
        for target, var, macro, folder in stale:
            body = " \\\n".join("  %s/%s" % (macro, f[len(folder):])
                                for f in cmake[target])
            text = re.sub(r"^" + var + r" := \\\n(?:.*\\\n)*.*$",
                          var + " := \\\n" + body, text, count=1, flags=re.M)
        (REPO / "mac" / "Makefile").write_text(text, encoding="utf-8",
                                               newline="")
        fixes.append("rewrote mac/Makefile's source lists from CMakeLists.txt")
        return True
    return False


def check_bridge_surface(problems):
    header = (REPO / "core" / "composer.h").read_text(encoding="utf-8")
    body = header[header.index("class Composer"):]
    body = body[:body.index("\n private:")]
    names = set()
    for match in re.finditer(r"^\s+(?:[\w:<>,\s\*&]+?\s)?(\w+)\s*\(", body, re.M):
        names.add(match.group(1))
    names -= {"Composer", "explicit", "if", "for", "while", "return", "function"}

    bridge = (REPO / "mac" / "src" / "ArtBridge.mm").read_text(encoding="utf-8")
    called = set(re.findall(r"_composer->(\w+)", bridge))

    allowed = set()
    if ALLOW.is_file():
        for line in ALLOW.read_text(encoding="utf-8").splitlines():
            line = line.split("#", 1)[0].strip()
            if line:
                allowed.add(line)

    uncovered = sorted(names - called - allowed)
    if uncovered:
        problems.append(
            "mac/src/ArtBridge.mm never calls: %s\n      Either expose them, "
            "or record why not in mac/parity-allow.txt." % ", ".join(uncovered))


def check_upstream_drift(problems, marker):
    commit = marker.get("commit", "")
    shallow = git("rev-parse", "--is-shallow-repository") == "true"
    if not commit:
        problems.append("mac/upstream-alignment.txt has no commit= value")
    elif not git("cat-file", "-t", commit):
        if shallow:
            # Not drift: a shallow clone simply does not contain the marker's
            # commit. Say which it is, or the report reads as a broken marker.
            print("note: shallow clone, so the drift log is unavailable "
                  "(checkout with fetch-depth: 0 to enable it)")
        else:
            problems.append("mac/upstream-alignment.txt points at %s, which "
                            "is not a commit in this repository" % commit)
    else:
        log = git("log", "--oneline", "%s..HEAD" % commit, "--", *WATCHED)
        if log:
            lines = log.splitlines()
            problems.append(
                "%d commit(s) touched the shared code since the last "
                "alignment:\n      %s\n      Read them against mac/src/, then "
                "move commit= in mac/upstream-alignment.txt."
                % (len(lines), "\n      ".join(lines)))

    for key, (path, name, mirror) in TRANSLITERATED.items():
        actual = function_body_hash(path, name)
        if actual is None:
            problems.append(
                "%s is gone from %s -- renamed or removed.\n      %s is a "
                "transliteration of it and is now orphaned."
                % (name, path, mirror))
        elif marker.get(key) != actual:
            problems.append(
                "%s changed in %s.\n      %s transliterates it, in the same "
                "order deliberately -- read both.\n      Then set %s = %s"
                % (name, path, mirror, key, actual))


def check_versions(problems, fixes, marker, fix):
    version = (REPO / "VERSION").read_text(encoding="utf-8").strip()

    makefile = (REPO / "mac" / "Makefile").read_text(encoding="utf-8")
    if "cat $(ROOT)/VERSION" not in makefile:
        problems.append("mac/Makefile no longer reads the root VERSION")

    package = (REPO / "scripts" / "make-package.ps1").read_text(encoding="utf-8")
    if "VERSION" not in package:
        problems.append("scripts/make-package.ps1 does not read the root VERSION")

    if marker.get("version") != version:
        if fix:
            write_marker({"version": version})
            fixes.append("set version = %s in mac/upstream-alignment.txt"
                         % version)
        else:
            problems.append(
                "mac/upstream-alignment.txt says version = %s, VERSION says %s"
                % (marker.get("version", "(none)"), version))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--fix", action="store_true",
                        help="repair the mechanical drift (never behaviour)")
    args = parser.parse_args()

    problems, fixes = [], []
    marker = read_marker()

    # The source lists are the only thing repairable without a judgement
    # call, so whether --fix is worth suggesting is decided here, before the
    # repaired complaints are dropped.
    repaired = check_source_lists(problems, fixes, args.fix)
    fixable = bool(problems)
    if repaired:
        problems.clear()
    check_bridge_surface(problems)
    check_upstream_drift(problems, marker)
    check_versions(problems, fixes, marker, args.fix)

    for fixed in fixes:
        print("fixed: %s" % fixed)
    if problems:
        print("")
        print("mac/ may be behind the Windows side:")
        print("")
        for problem in problems:
            print("  *  %s" % problem)
            print("")
        if fixable:
            print("Some of this is mechanical -- try --fix. The rest is a")
        else:
            print("None of this is auto-fixable by design: each one is a")
        print("judgement about behaviour. See .claude/skills/mac-parity/SKILL.md.")
        return 1
    print("ok: mac/ is aligned with the Windows side")
    return 0


if __name__ == "__main__":
    sys.exit(main())
