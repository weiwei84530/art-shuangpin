"""Restore vendor/ : the art-shuangpin mirror clone and the language model.

vendor/ is gitignored, so this is what makes a fresh checkout buildable.

Two invariants this script exists to enforce:

  * the mirror's push URL is broken on purpose, so an accidental `git push`
    inside vendor/art-shuangpin can never reach the source repository;
  * nothing here ever writes to the source.  Cloning and fetching are
    read-only operations on it.

Standard library only.
"""

import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
# The art-shuangpin working copy on this Windows host.  It moved from
# D:\\Claude\\Input to here on 2026-08-06; the fetch URL of an existing mirror
# is re-pointed below, so an older clone keeps working.
SOURCE = Path(r"D:\Projects\art-shuangpin")
MIRROR = REPO / "vendor" / "art-shuangpin"
DATA_DEST = REPO / "vendor" / "mspy-data.txt"

# `out/` is gitignored upstream, so the language model is not in the clone --
# it is a build product of `data/Makefile` (Python).  We copy the exact file
# the shipped Windows IME uses, and pin its hash so a silent swap is loud.
DATA_SOURCE = SOURCE / "out" / "data.txt"

PUSH_URL_DISABLED = "DISABLED-read-only-mirror"


def read_pin():
    """vendor.pin, the single place the pinned coordinates live.

    bootstrap.command sources the same file on the Mac.  Keeping the hash in
    one file rather than two is the point: a mismatch between what this script
    accepts and what the Mac downloads would be silent and confusing.
    """
    values = {}
    for line in (REPO / "vendor.pin").read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        values[key.strip()] = value.strip()
    return values


PIN = read_pin()
DATA_SHA256 = PIN["MSPY_DATA_SHA256"]
PINNED_TAG = PIN["ART_TAG"]


def run(args, cwd=None, check=True):
    proc = subprocess.run(args, cwd=cwd, check=False, capture_output=True, text=True)
    if check and proc.returncode != 0:
        sys.exit(f"$ {' '.join(args)}\n{proc.stdout}{proc.stderr}")
    return proc


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sync_mirror():
    if not (SOURCE / ".git").exists():
        sys.exit(f"art-shuangpin not found at {SOURCE}")

    if not (MIRROR / ".git").exists():
        MIRROR.parent.mkdir(parents=True, exist_ok=True)
        if MIRROR.exists():
            shutil.rmtree(MIRROR)
        # --no-hardlinks: a local clone would otherwise share object files with
        # the source repo.  Git objects are immutable so it is safe in theory;
        # we pay the 16 MB to not have to think about it.
        print(f"cloning {SOURCE} -> {MIRROR}")
        run(["git", "clone", "--no-hardlinks", str(SOURCE), str(MIRROR)])
    else:
        # Re-point the fetch URL first: the source has moved once already, and
        # a mirror cloned before the move would otherwise fail here rather
        # than say why.
        run(["git", "remote", "set-url", "origin", str(SOURCE)], cwd=MIRROR)
        print(f"fetching into {MIRROR}")
        run(["git", "fetch", "origin", "--tags", "--prune"], cwd=MIRROR)
        run(["git", "remote", "set-head", "origin", "--auto"], cwd=MIRROR, check=False)
        run(["git", "reset", "--hard", "origin/HEAD"], cwd=MIRROR, check=False)

    # Re-assert the broken push URL every run; a `git remote set-url origin`
    # by hand would otherwise silently restore write access to the source.
    run(["git", "remote", "set-url", "--push", "origin", PUSH_URL_DISABLED], cwd=MIRROR)

    push = run(["git", "remote", "get-url", "--push", "origin"], cwd=MIRROR).stdout.strip()
    if push != PUSH_URL_DISABLED:
        sys.exit(f"refusing to continue: mirror push URL is {push!r}, expected it disabled")

    described = run(["git", "describe", "--tags", "--always"], cwd=MIRROR).stdout.strip()
    dirty = run(["git", "status", "--short"], cwd=MIRROR).stdout.strip()
    print(f"  mirror at {described}" + ("  (DIRTY -- commit nothing here)" if dirty else ""))
    return described


def sync_data():
    if not DATA_SOURCE.exists():
        sys.exit(
            f"language model not found at {DATA_SOURCE}\n"
            f"build it first:  cd {SOURCE / 'data'} && make"
        )
    DATA_DEST.parent.mkdir(parents=True, exist_ok=True)
    if not DATA_DEST.exists() or sha256(DATA_DEST) != sha256(DATA_SOURCE):
        print(f"copying {DATA_SOURCE.name} -> {DATA_DEST}")
        shutil.copy2(DATA_SOURCE, DATA_DEST)

    got = sha256(DATA_DEST)
    size = DATA_DEST.stat().st_size
    if got != DATA_SHA256:
        print(f"  WARNING: mspy-data.txt sha256 is {got}")
        print(f"           pinned value is        {DATA_SHA256}")
        print("           the upstream data pipeline has been rebuilt; update")
        print("           MSPY_DATA_SHA256 in vendor.pin once you have confirmed")
        print("           that is intended -- the Mac bootstrap reads the same value")
        print("           and will refuse the download until it matches.")
    else:
        print(f"  mspy-data.txt {size:,} bytes, sha256 pinned OK")


def check_pin(described):
    """Warns when this tree has moved past the release the Mac would fetch.

    This script follows the source repo's HEAD; bootstrap.command clones a
    TAG. They are allowed to differ while a release is being tracked -- but
    silently, the Mac would then build something older than what is being
    tested here, so say it out loud.
    """
    if described == PINNED_TAG:
        return
    print()
    print(f"note: vendor.pin says {PINNED_TAG}, this mirror is at {described}.")
    print("      bootstrap.command on the Mac fetches the PINNED tag, so it")
    print("      will not get whatever is newer here until vendor.pin is bumped.")


def main():
    described = sync_mirror()
    sync_data()
    check_pin(described)
    print()
    print(f"vendor/ ready (art-shuangpin {described})")
    print("reminder: vendor/ is READ-ONLY. Never commit inside it, never write to")
    print(f"          {SOURCE}")


if __name__ == "__main__":
    main()
