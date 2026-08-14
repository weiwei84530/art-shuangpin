# CLAUDE.md — ArtShuangpin for macOS

A native macOS **InputMethodKit** shell around the art-shuangpin C++ core, which sits one
directory up: `../core`, `../engine`, `../cli`. This is the macOS half of the repository;
`../ime` is the Windows half, and both are shells over the same `mspy::Composer`.

Until 2026-08-13 this was a separate private repo consuming a read-only mirror of the core
under `vendor/`. Both are gone. The reason for the merge was the mirror's one real cost:
the shell repo was private, so every Mac needed GitHub credentials, while everything it
fetched into `vendor/` was already public.

The root `CLAUDE.md` governs and is 繁體中文; this file covers what is specific to `mac/`
and stays English (rule 5).

## What `mac/` is — and is not

This directory is a **shell only**. Every input behaviour — the Composing/Selecting state
machine, the reading grid, the cursor and anchor model, tone semantics, punctuation,
learning — already lives in `mspy::Composer`. The shell feeds it abstract keys and renders
its output.

* **Never re-implement composer logic in ObjC++.** If behaviour is wrong, the fix belongs in
  `../core`, not here. (That is now an edit you can make yourself — see rule 3 — which makes
  the temptation to patch around it in the shell easier to give in to, not harder.)
* **Behaviour spec: `../docs/spec.md` §6** and `../core/composer.h`. Read them before
  touching key handling. They are the authority; this file is not.

## Hard rules

1. **Development host is Windows; the target is macOS.** There is no darwin toolchain here —
   no Xcode, no `swiftc`, no clang for darwin. **Never try to build the app here.** What
   *can* be verified on this host is the shared C++: `cmake --build build && ctest` covers
   `../core`, `../engine` and `../cli`, and `../cli/repl.exe` answers any composer-behaviour
   question without a Mac. Everything under `src/` compiles only in
   `../.github/workflows/mac.yml` and behaves only on the user's Mac — write it, push it,
   read the CI log. Expect **171** tests on macOS against 173 here: `engine_tests` picks its
   `MemoryMappedFile` test per platform and the POSIX file has two fewer cases.
2. **Command Line Tools only.** The user has `xcode-select --install`, not full Xcode. Build
   with plain `clang++` driven by a `Makefile`, assemble the `.app` by hand, ad-hoc
   `codesign -s -`. **No `.xcodeproj`, no `xcodebuild`, no nib/xib** — `ibtool` ships with
   Xcode, not with CLT. CI runs on a **full-Xcode runner**, so a green build is *not* proof
   of this rule: never introduce an Xcode-only tool because CI accepted it. CMake is not part
   of CLT either, which is why `make probe` links `../cli/repl.cpp` by hand rather than
   through `../CMakeLists.txt`.
3. **`../core`, `../engine` and `../cli` are the same tree now** — not a mirror, and no
   longer off limits. You may change them, and a change there **is a Windows change**: run
   `ctest` before committing, and check `src/ArtBridge.mm` still exposes what the shell
   needs. `scripts/check-parity.py` reports both. What has not changed is where behaviour
   belongs: editing `../core` to work around something awkward in the shell is still the
   wrong fix.
4. **The language model is a build product.** `../out/data.txt`, 7.5 MB, built from the
   tracked sources in `../data` by `bash ../scripts/build-data.sh` — python3, no packages, a
   couple of minutes. `out/` is gitignored, so no clone carries it, and CI caches it keyed on
   the git tree object of `data/`. No pinned tag, no release asset, no sha256 to keep in step
   any more; `make` diagnoses a missing model and prints the one command that fixes it.
5. **Language — by reader, not by directory.** This file and `docs/NOTES.md` are **English**:
   their readers are people editing `src/`, the vocabulary (IMKInputController, marked text,
   TCC, code directory hash) is English anyway, and translating them would cut them off from
   the Windows-side comments they are meant to be read beside. `docs/INSTALL.md`,
   `release/README.txt` and the *printed output* of every `*.command` are **繁體中文** —
   those are read by the person installing, not by whoever is editing the repo. Comments
   inside those same scripts stay English. Replies to the user are 繁體中文. The root
   `CLAUDE.md` states this ruling once; do not "fix" either half towards the other.
6. **Git.** One public `origin`, `weiwei84530/art-shuangpin`, shared with the Windows half.
   The root rule governs: commit freely, **push only when asked**, stay on the current
   branch. New since the merge: **a tag is a publishing act** — pushing `vX.Y.Z` triggers
   `release-mac.yml`, which builds the app and attaches it to a GitHub release. Treat
   tagging exactly like a push, and never tag unasked.
7. **Commit messages, tag annotations and code comments are English.** Release notes are
   **繁體中文**, per the root rule — there is one release page for both platforms now, and
   its language has been consistent since v0.2.

## Why this project exists — the measured root cause, do not re-derive

There is an earlier attempt at `D:\Claude\InputMac` (commit `d349808`): a RIME/Squirrel
configuration package. It reproduces roughly 70% of art-shuangpin and then hits a wall that is
**structural, not cosmetic**. Measured through `RimeGetContext` while aiming at syllable 2 of
`ni3hk3vs` (你好中):

```
composition.preedit = '你haoˇvs'   sel = [3,8)   <- full length, anchor range is correct
commit_text_preview = '你好'                      <- converted, but truncated at the caret
```

librime converts only the input **left of the caret**; the tail is raw input that no segment
owns. The Chinese for 中 therefore does not exist anywhere in the engine, and no amount of
patching Squirrel's display layer can render it. librime's composition is a *prefix-lock*
model with no per-character anchor; art's is a McBopomofo *reading grid* that has one.

Three things follow, and none of them is reachable from Rime:

* the full sentence staying visible while a middle character is being changed,
* the pale-blue anchor on the character to the **RIGHT** of the cursor,
* idle `9`/`0`/`-`/`=` navigation after the text has been committed.

art's own core already implements all three. **Do not go back to Rime**, and do not propose
patching librime or Squirrel — the engine, not the display layer, is what differs.

## The seam — `mspy::Composer`, measured

`core/composer.h` is deliberately TSF-free. The whole shell contract is:

```cpp
// v0.6: RelaxedToneLM again — the learned-preference layer above it is
// gone, its job moved inside the composer (see setPreferences below).
Composer(std::shared_ptr<Formosa::Gramambular2::LanguageModel> lm);

State state() const;                    // kEmpty | kComposing | kSelecting
bool  wouldConsume(char c) const;       // side-effect free eat/pass decision
Result feedChar(char c);                // {bool consumed; std::string commitText;}
Result feedBackspace(); feedEnter(); feedEsc();
Result selectCandidate(size_t index); Result closeCandidateMenu(); void cancel();

// v0.5. Both return consumed=false when the composer is kEmpty, and NEITHER
// ever commits: English joins the same uncommitted buffer as literal text,
// and the language switch only settles what is in progress and drops in the
// half-width separator space.
Result feedEnglishChar(char c);
Result switchLanguage(bool toEnglish);

struct DisplaySegments { std::string before, unconfirmed, highlighted, after; };
DisplaySegments displaySegments() const;

const std::vector<...::Candidate>& candidates() const;
std::vector<...::Candidate> currentPageCandidates() const;   // <= 6
size_t candidatePageIndex() const; size_t candidatePageCount() const;

// v0.6. Learning is no longer a model layer: the composer OWNS the store,
// reads it on every walk and writes a manual pick into it. With no store
// set it simply does not learn.
void setPreferences(std::shared_ptr<UserPreferences> preferences);
const std::shared_ptr<UserPreferences>& preferences() const;

// Fires AFTER the pick has been written. The shell's only job is to
// persist the file; it never calls record() itself.
std::function<void(const std::string& context, const std::string& reading,
                   const std::string& value)> onLearned;
```

**`displaySegments().highlighted` IS the anchor** — "the single character right of the cursor"
(spec §6 「游標錨點反白」). The caret sits between `unconfirmed` and `highlighted`. This is
why the port is a shell and not a reimplementation: the thing the Rime version could not do is
a field the core already returns.

`../ime/SampleIME/MspyBridge.{h,cpp}` is the Windows equivalent of the bridge this directory
needs — **read it before changing `ArtBridge.mm`**, the two are deliberately line-for-line
comparable. `../cli/repl.cpp` builds the same three layers:

```cpp
auto lm = std::make_shared<McBopomofo::McBopomofoLM>();
lm->loadLanguageModel(dataPath);                       // mspy-data.txt
auto relaxed = std::make_shared<mspy::RelaxedToneLM>(lm);
mspy::Composer composer(relaxed);                      // v0.6: RelaxedToneLM, not a 4th layer
auto preferences = std::make_shared<mspy::UserPreferences>();
composer.setPreferences(preferences);                  // learning is INSIDE the composer
```

**There is no user-preference language model any more** (v0.6 deleted `UserPreferenceLM`).
A learned pick is applied as a high-score *node override on the reading grid*, so one
correction is enough; scoring layers could never manage that, because winning a reading is
not the same as winning the walk. Two designs died proving it — spec §7 決策記錄 2026-08-09
has the measurements. `McBopomofoLM::loadUserPhrases` is still deliberately **never called**:
it scores every user phrase at 0, which beats every dictionary entry.

## Portability — measured 2026-08-12 at v0.6 (non-test `.cpp`/`.h`), do not re-check

| tree | lines | platform |
|---|---|---|
| `../core` (composer, double pinyin, tone LM, user preferences) | 2272 | **zero Windows dependencies** |
| `../engine` (McBopomofo gramambular2 + LM) | 4419 | Windows code only in `MemoryMappedFile.{h,cpp}`, always behind `#ifdef _WIN32` with a POSIX `#else` |
| `../ime/SampleIME` (TSF shell) | 19090 | Windows-only — **replaced, not ported** |

`../cli` joined that first row on 2026-08-13: `repl.cpp` and `drill_gen.cpp` used to take
`wchar_t` argv, and now keep `wmain` behind `#ifdef _WIN32` with a plain `main` elsewhere.
`repl` is what `make probe` builds.

v0.6 also added `../drills` and ~3,000 lines of `../web` — a typing tutor. None of it is
input behaviour and none of it builds here; ignore it when reading a diff.

The `MemoryMappedFile.cpp` POSIX branch is the *upstream-native* one: the engine is
McBopomofo's, and McBopomofo is a macOS input method. This C++ compiles on darwin as-is.

## The port map

| art-shuangpin (Windows) | here (macOS) |
|---|---|
| `ime/SampleIME/` (TSF) | `src/` (InputMethodKit, ObjC++) |
| `MspyBridge.cpp` | `src/ArtBridge.mm` |
| `KeyEventSink.cpp` → `InjectNavigationKey` (`SendInput`) | `src/ArtNavigation.mm` (`CGEventPost`) |
| `CandidateWindow.cpp` (1492 lines) | `src/ArtCandidateWindow.mm` (roll our own `NSPanel`) |
| `%APPDATA%\MspyIME\user-choices.txt` (v0.6; the old `user-phrases.txt` is parked as `.bak`) | `~/Library/Application Support/ArtShuangpin/user-choices.txt` |
| `MoveFileEx(…, MOVEFILE_REPLACE_EXISTING)` for the atomic store rewrite | `std::rename` over the same directory |
| `mspy-data.txt` beside the DLL | `ArtShuangpin.app/Contents/Resources/mspy-data.txt` |

Use **ObjC++ (`.mm`)**, not Swift: the core is C++ and InputMethodKit is Objective-C, so `.mm`
talks to both with no bridging layer, and it keeps the build to one compiler.

## macOS specifics already settled by research

* **Ad-hoc signing is enough.** Locally built apps carry no quarantine attribute, so Gatekeeper
  never runs on them. Squirrel's own Makefile treats Developer ID signing as optional and
  installs an unsigned build into `/Library/Input Methods` — same situation.
* **CLT ships the frameworks.** `/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/
  Library/Frameworks` includes the public frameworks, InputMethodKit among them.
* **Idle navigation needs Accessibility.** `CGEventPost` only works if the app is trusted in
  System Settings → Privacy & Security → Accessibility. Prompt with
  `AXIsProcessTrustedWithOptions(@{kAXTrustedCheckOptionPrompt: @YES})`. There is no way
  around it — an input method cannot otherwise move another app's insertion point. The user has
  explicitly accepted this.
* **The injected arrow re-enters our own controller.** No tagging or loop-guard is needed as
  long as arrow keys are always passed through while idle — this is exactly how art does it on
  Windows, see the comment at `ime/SampleIME/KeyEventSink.cpp:55`.
* **The caret and the anchor travel on separate channels.** *Measured on the Mac; this
  supersedes the original guess, which was that `selectionRange` = the anchor's range would
  make a host draw the highlight AND put the caret at its start. It did neither: the caret
  stayed pinned to the end of the composition while the cursor was in fact mid-buffer.*
  What works is one signal per channel —
  `selectionRange` = a **zero-length** range at the cursor for position, and
  `NSUnderlineStyleThick` over the anchor character for emphasis, which is macOS's own idiom
  for "the segment being worked on" (Japanese input marks the active clause this way).
  `NSBackgroundColorAttributeName` is sent as well and is simply dropped by hosts that do not
  render attributed marked text. The rest of the composition is the app's default text colour
  with a **solid** thin underline: spec §6 asks for a dotted one, but Chromium hosts reduce
  the attribute to `[style intValue] > 1` and `NSUnderlineStyleSingle | NSUnderlinePatternDot`
  is 257 — dotting it flattens every character to "thick" and the anchor disappears.
* **Candidate window placement.** `[client attributesForCharacterIndex:lineHeightRectangle:]`
  with the anchor's index in the marked text gives the anchor character's screen rect — that is
  what spec §6 「候選窗開在游標錨點字的正下方」 needs.
* **Roll our own candidate panel.** `IMKCandidates` cannot do the required styling, the
  1–6 digit labels, or per-character positioning.

## Key ownership — shell vs composer

The composer owns everything **while composing**. The shell owns only:

* **the idle editing layer** (spec §6 「閒置編輯層」, v0.8): with nothing composing the
  whole unshifted digit row is replayed as an editing keystroke — `1` 行首, `2`/`3`
  select to 行首/行尾, `4` 行尾, `5` ⌦, `6` ⌫, `7`/`8` ↑/↓, `9`/`0` ←/→. **Only the
  unshifted digits**, so Shift+9 still types （ and Shift+1 still types ！, exactly as in
  Weasel. `-`, `=` and **Tab** are no longer intercepted at all (v0.8 removed them);
  handing Tab back is also what retires the Chromium focus-stealing failure recorded in
  docs/NOTES.md. This layer is the one thing English mode shares with Chinese mode, so
  the habit never has to be switched — see `-injectIdleEditingKeyIfWanted:`, which both
  branches call.
* **bare Shift tap** → `switchLanguage(toEnglish)` + toggle 中/英. Since v0.5 it
  **commits nothing** (spec §6 「中英切換」v5): the composition survives the switch and the
  composer inserts the separator space into its own buffer. The shell must never try to
  work out what is left of the caret — the passive last-character memory that used to do
  that (`ArtCharClass`, the commit-time caret snapshot, the global mouse monitor) was
  deleted with this change, and wanting it back means the change belongs elsewhere.
* **English-mode keys while a composition is live** →
  `-handleEnglishKeyDown:client:shift:`, the transliteration of upstream's
  `IsVirtualKeyNeedMspyEnglish`: Backspace deletes, Enter commits, Esc clears, arrows
  are eaten, printable ASCII (Space and **digits** included, case kept) goes to
  `feedEnglishChar` — which is what keeps a run like "user123" typable without a numeric
  keypad. With **nothing** composing, English mode passes every key straight through
  except the idle editing layer above.
* **per-application 中/英 memory** (spec §6, upstream v0.3). Every application starts in
  **English** and keeps its own mode, restored silently in `-activateServer:`. The Windows
  build gets this nearly free — a TSF text service runs inside the application's process,
  so the object already is the per-app slot. Here one process serves every application, so
  the slot is explicit: a dictionary keyed by bundle ID, dropped when the application
  quits (spec says 行程). Unlike Windows, our own menu switch is remembered too — upstream
  excludes its language-bar toggle only because it cannot tell that click apart from the
  system's writes to the shared compartment, and we have no such ambiguity.
* arrow keys / PgUp / PgDn eaten with no effect **while composing** (spec §6).
* NumPad exemption (spec §6): idle NumPad passes through; composing NumPad commits first.
* drawing, positioning, preference-store persistence.

Everything else: call `wouldConsume(c)` to decide eat/pass, then `feedChar(c)`.

**Since v0.4, nothing auto-commits — and since v0.5 the Shift switch does not either.**
Punctuation settles and joins the composition instead of sending it to the application, so a
composition can run indefinitely; only Enter, Space with nothing left to settle, a NumPad key
or losing focus produce text. Two consequences for the shell, both already handled — do not
undo them:

* *consumed* very often means an **empty** `commitText` with the composer still composing.
  Never write a handler that assumes consumed implies committed.
* `-deactivateServer:` is now the only thing standing between a long composition and its
  being lost when focus moves, so it must keep committing rather than cancelling.

The digit keys also gained a second meaning inside the composer (tone keys while a syllable is
unsettled, including the right-hand mirror `0`=1 `9`=2 `8`=3 `7`=4 `6`=5). That is entirely
`wouldConsume`/`feedChar`'s business — the shell's idle-navigation block runs only when the
composer is empty, so the two never contend. v0.5 narrowed the tone window (a tone digit now
settles the syllable, so `8` opens the menu immediately afterwards) and made lone first keys
whole syllables; v0.6 extended that to **all 26 letter keys** (`d`+Space = 的, `n`+`4` = 訥)
and stopped a candidate pick re-wording the rest of the sentence. All of it is invisible from
here — `wouldConsume`/`feedChar`/`selectCandidate` absorb every one.

## Things that look like defects and are not

Each of these reads as "the Mac has fallen behind Windows". Report them if you
must, but do not change them without being asked.

* **The order of the tests in `-handleKeyDown:client:`.** It is load-bearing:
  the idle navigation keys have to be taken off the table before
  `wouldConsume()` is asked, because that claims every idle digit. Reordering
  it looks like a tidy-up and is a bug.
* **`-`/`=` post ⌘← / ⌘→ rather than Home/End.** On macOS, Home and End mean
  document start and end, not line start and end.
* **Per-application 中/英 is an explicit dictionary keyed by bundle id.** The
  Windows build gets the same behaviour for free because a text service runs
  inside each application's process; one IMKServer serves every application,
  so the slot has to be explicit here.
* **The marked-text underline is solid where spec §6 asks for dotted.**
  Chromium hosts reduce the attribute to `[style intValue] > 1`, and
  `NSUnderlineStyleSingle | NSUnderlinePatternDot` is 257 — dotting it flattens
  every character to "thick" and the anchor disappears.
* **`resources/Info.plist`'s `ComponentInputModeDict`.** Getting it wrong makes
  the input method vanish from the input-source list entirely.
* **`commit` and the two function hashes in `upstream-alignment.txt`.** Moving
  those is a person asserting the review happened. `check-parity.py --fix`
  refuses to touch them and so should you.

## Cutting a release

One release page, two assets, one `VERSION` at the repository root. `git push` of a tag
`vX.Y.Z` **is the publishing act** for this half (rule 6).

1. Bump `../VERSION`, commit.
2. `git tag -a vX.Y.Z -m "…"` and push the tag **when asked**.
   `release-mac.yml` then checks the tag equals `v$(cat VERSION)` — that gate is the whole
   enforcement that the two halves agree — builds a universal app on `macos-15`, packs it
   with `ditto` and attaches `art-shuangpin-mac-vX.Y.Z.zip` to a **draft** release.
3. On the Windows box: build both architectures, `scripts\make-package.ps1` →
   `art-shuangpin-vX.Y.Z.zip`, `gh release upload` it to the same tag.
4. Write the 繁體中文 notes covering both halves, then publish the draft.

**Why the zip and not a bare `.command`.** A `.command` served over HTTP arrives with no Unix
mode — browsers save it `0644` — so downloading the bare file and double-clicking reproduces
exactly the *"you do not have appropriate access privileges"* dialog. Only an archive carries
the executable bit, which is also why `tools/verify_zip.py` checks it. Release notes must say
**right-click → 打開**, not double-click: downloads are quarantined and this is ad-hoc signed,
not Developer ID.

`release/` holds exactly what goes into that archive beside the app. `install.command` there
is a different file from the one in this directory: it copies a prebuilt app rather than
building one, strips `com.apple.quarantine`, and resets the Accessibility grant every time —
see NOTES.md, "Ad-hoc signing and Accessibility".

## Testing

The app cannot run on Windows, but almost everything under it can be checked there:

* **The C++ test suite is in this repository now** — `../core/*_test.cpp`,
  `../engine/**/*Test.cpp`. `cmake --build build && ctest` on the Windows host is the same
  coverage the Mac gets, minus two platform-specific mmap cases.
* `../build/Release/repl.exe --keys "ni3hk3vs 99"` prints every state transition including
  `anchor:[...]`. (The space is load-bearing since v0.4: while `vs` is unsettled the digits
  are tone keys, not cursor keys.) `#` is the bare-Shift switch, so `--keys "ni3hk3# ok#"`
  replays the v0.5 中英 flow without a Mac. Use it to establish expected behaviour **before**
  writing shell code, so the Mac-side loop is only ever debugging the shell.
  `--user-choices <path>` loads a copy of the user's learned store so the output matches what
  they actually see; since v0.6 those records are grid overrides, so they change what comes
  out rather than merely how candidates rank — reproducing a report without the file
  reproduces a different engine.
* On the Mac, `make probe` builds that same `repl` from `../cli/repl.cpp` and
  `check-engine.command` runs it. It used to be a local copy called `artprobe` with its own
  key dialect (`~` for Shift, `#` for Enter); if you find that spelling anywhere, it is stale.
* `python scripts/check-parity.py` from the repository root reports what the Windows side has
  changed since `upstream-alignment.txt`, and what this shell may owe it.

The shell itself is verified by CI (it compiles, links universal, signs) and by the user on
the Mac (it behaves). Keep it thin so there is little to verify.
