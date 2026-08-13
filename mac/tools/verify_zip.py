#!/usr/bin/env python3
"""Check a release ZIP for the things that only ever fail on the far side.

    python3 mac/tools/verify_zip.py out/art-shuangpin-mac-v0.7.0.zip

The release archive is built by `ditto` on a macOS runner, which gets all of
this right natively. This exists anyway because the lessons below cost real
debugging to learn, they are invisible on the host that builds the archive,
and the day someone assembles the zip somewhere other than macOS -- with
PowerShell's Compress-Archive, or Python's own zipfile -- every one of them
comes back at once.

  * Entry names use forward slashes. Compress-Archive is known to write
    backslashes, which macOS extracts as files with literal backslashes in
    their names. Checked against ZipInfo.orig_filename, not .filename:
    ZipInfo.__init__ rewrites os.sep to "/", so on Windows the decoded name
    never contains a backslash and the obvious check is dead code. It was
    dead in the script this one came from -- measured on an archive that
    really did store one in its central directory.

  * The executable bit survives. Writing the mode into external_attr is only
    HALF of it, and the missing half is silent: zipfile.ZipInfo sets
    create_system = 0 (MS-DOS) when constructed on Windows, and an extractor
    reading a create_system-0 entry treats the low byte as DOS attributes and
    IGNORES the Unix mode in the high 16 bits. The archive then looks correct
    to zipfile on the build host and extracts 0644 on the Mac. Symptom:
    double-clicking install.command gives "could not be executed because you
    do not have appropriate access privileges", and Get Info shows read and
    write with no execute. The same applies to the app's own binary, which
    simply will not launch.

  * No CR reaches a shell script. One on the shebang line makes bash fail
    with "bad interpreter: /bin/bash^M", which reads as a corrupt file rather
    than a line-ending problem. The root .gitattributes pins these to LF;
    this is the check that says so out loud if something wrote one anyway.

Standard library only. Read-only: it never modifies the archive.
"""

import pathlib
import sys
import zipfile

EXECUTABLE_SUFFIXES = {".sh", ".command"}


def verify(path: pathlib.Path) -> list[str]:
    problems = []
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()

        names = [i.filename for i in entries]
        if not any(n.endswith(".command") for n in names):
            problems.append("no .command files in the archive at all")

        binaries = [
            n for n in names
            if "/Contents/MacOS/" in n and not n.endswith("/")
        ]
        if not binaries:
            problems.append(
                "no <name>.app/Contents/MacOS/<binary> -- this is not an app bundle"
            )
        if not any("/Contents/Info.plist" in n for n in names):
            problems.append("no Contents/Info.plist -- the bundle is incomplete")

        for info in entries:
            name = info.filename
            stored = getattr(info, "orig_filename", name)
            if "\\" in stored:
                problems.append(f"{stored}: backslash in the entry name")
            if info.create_system != 3:
                problems.append(
                    f"{name}: create_system={info.create_system}, so the Unix "
                    f"mode is ignored on extraction"
                )
            if name.endswith("/"):
                continue

            mode = (info.external_attr >> 16) & 0o777
            suffix = pathlib.PurePosixPath(name).suffix
            must_execute = suffix in EXECUTABLE_SUFFIXES or name in binaries
            if must_execute and not mode & 0o100:
                problems.append(f"{name}: mode {oct(mode)}, owner cannot execute")
            if not mode & 0o400:
                problems.append(f"{name}: mode {oct(mode)}, owner cannot read")

            if suffix in EXECUTABLE_SUFFIXES and b"\r" in archive.read(name):
                problems.append(
                    f"{name}: contains a CR; bash would fail with "
                    f'"bad interpreter: /bin/bash^M"'
                )
    return problems


def main() -> int:
    if len(sys.argv) != 2:
        return int(bool(sys.stderr.write(__doc__.split("\n\n")[1] + "\n")))
    path = pathlib.Path(sys.argv[1])
    if not path.is_file():
        sys.stderr.write(f"no such archive: {path}\n")
        return 1
    problems = verify(path)
    if problems:
        sys.stderr.write(
            "the archive is wrong; do NOT publish it:\n  "
            + "\n  ".join(problems[:20])
            + "\n"
        )
        return 1
    print(f"ok: {path.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
