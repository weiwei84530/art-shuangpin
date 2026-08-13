---
name: mac-parity
description: Audit what the Windows side has changed since the macOS shell last kept up, decide what mac/src/ owes it, and apply the purely mechanical fixes. Use when the user says "檢查 mac 對齊", "mac 要跟上嗎", "parity check", after changing core/ engine/ or the key routing in ime/, or before cutting a release.
---

# Keeping the macOS shell in step

`ime/` and `mac/` are two shells over one `mspy::Composer`. Nothing notices
when one falls behind, because `mac/` compiles only in CI and behaves only on
the user's Mac. This is the check.

**Set expectations low before starting.** `mac/docs/NOTES.md` records what
each upstream release actually cost this shell: v0.3 one file, v0.4 one, v0.5
two, v0.6 one. Most core changes reach the Mac for free, because
`ArtBridge.mm` is a mechanical passthrough. The likely honest answer is
"nothing to do here" — say that plainly rather than inventing work.

## 1. Run the audit

```sh
python scripts/check-parity.py
```

Exit 0 means aligned; stop and say so. Otherwise it reports some of:

- **source lists drifted** — `mac/Makefile` against `CMakeLists.txt`. Always a
  bug, always mechanical, and a link error only the Mac would ever see.
- **`ArtBridge.mm` never calls X** — `core/composer.h` gained a public method
  the shell does not expose. Sometimes correct; see `mac/parity-allow.txt`.
- **N commits touched the shared code** — the drift log. This is the one that
  needs reading.
- **`IsVirtualKeyNeedMspy` changed** — the transliteration signal. See below.

## 2. Read the drift

For each commit in the log, decide which of three it is. Report as a table.

| verdict | what it means |
|---|---|
| **none** | Behaviour inside the composer. `wouldConsume`/`feedChar`/`selectCandidate` absorb it; the Mac gets it for free. This is most of them. |
| **bridge** | The `mspy::Composer` surface changed — a new method, a renamed callback, a different file for the preference store. `mac/src/ArtBridge.mm` needs the corresponding line. |
| **shell** | Key routing, or something a TSF concept has no AppKit equivalent for. Read `ime/`'s change and `mac/src/ArtInputController.mm` side by side. |

Two things make this cheaper than it sounds: `docs/spec.md` §6 is the
behaviour authority for both shells, and `mac/docs/NOTES.md` has the per-release
tables as worked precedent — read the v0.6 one before judging a new release.

## 3. The transliteration hashes

`-handleKeyDown:client:` reproduces `IsVirtualKeyNeedMspy` **in the same
order**, and the order is load-bearing: idle navigation keys must be taken off
the table before `wouldConsume()` is asked, because it claims every idle digit.
No tool can verify that semantically, so the marker holds a hash of the
Windows function's body.

A changed hash means **go and read both functions**. It does not mean the Mac
is wrong — a comment edit changes the hash too. Decide, then have the user
update the value in `mac/upstream-alignment.txt`.

If the tool reports the function is *gone*, that is a rename, and the Mac's
routing has just been orphaned without any other signal. Treat it as urgent.

## 4. Apply the mechanical fixes

```sh
python scripts/check-parity.py --fix
```

Show the diff. It only ever rewrites `mac/Makefile`'s source lists (derived
from `CMakeLists.txt`) and the `version` line in the marker.

## Never change without being asked

Each of these looks like a defect and is not. Report them; do not "fix" them.

- **Any ObjC++ behaviour.** Path fixes in comments are fine; logic is not.
- **The order of tests in `-handleKeyDown:client:`.** Reordering reads as a
  cleanup and is a bug — see above.
- **`commit` or either hash in `mac/upstream-alignment.txt`.** Moving those is
  the user asserting the review happened. `--fix` will not touch them and
  neither should you.
- **The deliberate macOS divergences**, all documented in
  `mac/docs/NOTES.md` under "Deliberate differences from the Windows build":
  `-`/`=` post Cmd+←/→ rather than Home/End (Home/End mean document start and
  end on macOS); per-app 中/英 is an explicit bundle-id dictionary; the mode
  indicator is a HUD; the marked-text underline is **solid** where spec §6 asks
  for dotted, because Chromium hosts flatten
  `NSUnderlineStyleSingle|NSUnderlinePatternDot` (= 257) to "thick" and the
  anchor disappears.
- **`mac/resources/Info.plist`'s `ComponentInputModeDict`.** Getting it wrong
  makes the input method vanish from the input-source list entirely.
- **`git commit`, `push` or `tag`.** The root Git rule governs, and a tag now
  triggers the release workflow.

## Before a release

Alignment is a precondition for tagging: the tag builds and publishes the Mac
app. Run the audit, resolve or consciously accept whatever it reports, and
check `mac/docs/NOTES.md` has a section for the version being cut.
