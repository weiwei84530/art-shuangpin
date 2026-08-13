# ArtShuangpin for macOS

A native macOS **InputMethodKit** input method that runs the *existing* art-shuangpin
conversion core — the same C++ that ships in the Windows TSF build — behind a Cocoa shell.

Private project. `origin` is the private GitHub repo `weiwei84530/art-shuangpin-mac`.

## Status

**Working**, tracking upstream art-shuangpin **v0.6**. Builds with Command Line Tools and has
been installed and typed with on the Mac; some behaviour is still being tuned. Note that the
development host is Windows and has no darwin toolchain — nothing here is ever compiled where
it is written, so the Mac is the only place anything is verified. `docs/NOTES.md` lists what
to check first when it does not work, and has the per-release record of what upstream changed
and what it cost the shell.

## Why not Rime

There is an earlier RIME/Squirrel attempt at `D:\Claude\InputMac`. It gets about 70% of the
way and then stops, for a reason that is structural rather than cosmetic: librime converts
only the input to the left of the caret, so while you are changing a character in the middle
of a sentence the rest of that sentence does not exist anywhere in the engine. Measured, on
`ni3hk3vs` (你好中) with the second syllable aimed:

```
composition.preedit = '你haoˇvs'   sel = [3,8)
commit_text_preview = '你好'
```

No display-layer patch can render a character the engine never produced. librime's
composition is a prefix-lock model; art's is a McBopomofo reading grid with a real
per-character anchor. Three things follow directly and are unreachable from Rime:

* the full sentence staying visible while a middle character is being changed,
* the anchor emphasis on the character to the **right** of the cursor,
* idle `9`/`0`/`-`/`=` cursor navigation after the text has been committed.

art's core already returns all three. `Composer::displaySegments()` hands back
`{before, unconfirmed, highlighted, after}` where `highlighted` *is* the anchor character.
So this project is a shell, not a reimplementation.

## Layout

```
Makefile      the whole build: clang++, a hand-assembled .app, codesign -s -
vendor.pin    pinned upstream tag + release asset + language-model sha256;
              read by both tools/sync_art.py and bootstrap.command
*.command     double-clickable wrappers; bootstrap.command clones this repo
              and rebuilds vendor/ from public sources, so installing needs
              neither Terminal nor a hand-carried archive
src/          the ObjC++ InputMethodKit shell
  ArtBridge          the engine stack and the learned preference store;
                     the only file containing C++
  ArtInputController key routing (spec §6), marked text, mode, menu
  ArtCandidateWindow hand-rolled NSPanel, opened under the anchor character
  ArtModeHUD         the 中/英 flash
  ArtNavigation      CGEventPost for the idle navigation keys
resources/    Info.plist template and localized strings
scripts/      install.sh / uninstall.sh, run on the Mac
tools/        sync_art.py (Windows side), artprobe.cpp + make_icon.m (Mac side)
docs/         INSTALL.md (繁中, ships as README.txt), NOTES.md
vendor/       gitignored; restored by tools/sync_art.py
  art-shuangpin/   read-only mirror clone of D:\Projects\art-shuangpin
  mspy-data.txt    the 7.5 MB language model
```

## Getting a working tree

On the Windows host, from the local art-shuangpin working copy:

```
python tools/sync_art.py
```

Clones (or fetches) the art-shuangpin mirror and copies the language model in. `vendor/` is
gitignored, so this is required after a fresh checkout.

On the Mac there is no local working copy to copy from, so `bootstrap.command` rebuilds
`vendor/` from public sources instead — the upstream repo at the tag in `vendor.pin`, and the
language model out of that tag's release asset, sha256-checked. It clones this repo first if
it is not already there, which makes it both the install and the update path. Only the clone
of *this* repo needs credentials; everything in `vendor/` is public.

## Building

On the Mac, with Command Line Tools only:

```
xcode-select --install
make && make install
```

`vendor/` is gitignored and does not travel with a clone — copy it over from the Windows
host first, with something that leaves line endings alone. `make` says so precisely if it
is missing.

There is no Xcode project and no nib — `ibtool` is not part of CLT. The build is plain
`clang++` plus a hand-assembled `.app` bundle, ad-hoc signed. Nothing here can be built on
the Windows development host.

`make probe` builds `build/artprobe`, which links the conversion core and nothing else:

```
make probe
build/artprobe --data vendor/mspy-data.txt --keys "ni3hk3vs#"
```

That answers "is the engine fine on darwin?" without involving InputMethodKit, which is
the first question worth answering when something breaks. (The upstream `cli/repl.cpp`
cannot be used for this: it includes `<windows.h>`, and `vendor/` is read-only.)

## The one permission it needs

Idle `9`/`0`/`-`/`=` navigation posts real arrow keystrokes with `CGEventPost`, which
requires the app to be trusted under System Settings → Privacy & Security → Accessibility.
Everything else works without it.
