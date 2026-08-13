# CLAUDE.md — ArtShuangpin for macOS

A native macOS **InputMethodKit** shell around the *existing* art-shuangpin C++ core
(`D:\Projects\art-shuangpin`, mirrored read-only under `vendor/art-shuangpin/`).
The mirror currently tracks upstream **v0.6**.

Private project. `origin` is the **private** GitHub repo `weiwei84530/art-shuangpin-mac`;
`vendor/` never goes there. See rule 6.

## What this repo is — and is not

This repo is a **shell only**. Every input behaviour — the Composing/Selecting state machine,
the reading grid, the cursor and anchor model, tone semantics, punctuation, learning — already
lives in `mspy::Composer`. The shell feeds it abstract keys and renders its output.

* **Never re-implement composer logic in ObjC++.** If behaviour is wrong, the fix belongs
  upstream in art-shuangpin, not here.
* **Behaviour spec: `vendor/art-shuangpin/docs/spec.md` §6** and `core/composer.h`. Read them
  before touching key handling. They are the authority; this file is not.

## Hard rules

1. **Development host is Windows; the target is macOS.** There is no darwin toolchain here —
   no Xcode, no `swiftc`, no clang for darwin. **Never try to build or run the app.** The user
   builds on the Mac and reports back. Everything shipped is source.
2. **Command Line Tools only.** The user has `xcode-select --install`, not full Xcode. Build
   with plain `clang++` driven by a `Makefile`, assemble the `.app` by hand, ad-hoc
   `codesign -s -`. **No `.xcodeproj`, no `xcodebuild`, no nib/xib** — `ibtool` ships with
   Xcode, not with CLT.
3. **`vendor/art-shuangpin/` is a READ-ONLY mirror.** Never commit inside it. Its push URL is
   deliberately set to `DISABLED-read-only-mirror` so an accidental push cannot reach the
   original. **Never write to `D:\Projects\art-shuangpin`** under any circumstance. (It lived
   at `D:\Claude\Input` until 2026-08-06; `tools/sync_art.py` re-points an older mirror's
   fetch URL automatically.) Refresh the mirror with `python tools/sync_art.py`.
4. **`vendor/` is gitignored** — it holds the mirror clone and the 7.5 MB `mspy-data.txt`
   build product. Two ways to restore it, both driven by **`vendor.pin`** (the one place the
   upstream tag, release asset and data sha256 live — bump it to track a new release):
   `tools/sync_art.py` on Windows, from the local working copy; `bootstrap.command` on the
   Mac, from public sources. Gitignoring `vendor/` costs nothing now: neither half of it is
   private, only this repo is.
5. **Language.** Code, comments, `CLAUDE.md`, `README.md`, `docs/NOTES.md` are **English**.
   `docs/INSTALL.md` is **繁體中文** — it is end-user instruction text and ships as
   `README.txt`. The same rule makes the *printed output* of `*.command` and of
   `START-HERE.txt` 繁體中文 while their comments stay English: those are read by the
   person installing, not by whoever is editing the repo. Replies to the user are 繁體中文.
6. **Git.** `origin` is a **private** GitHub repo. Commit and push only when explicitly
   asked; stay on the current branch. What must never reach any remote is `vendor/` — it
   is gitignored, and the mirror clone inside it has its own push URL disabled (rule 3).
   The upstream `weiwei84530/art-shuangpin` is a *different*, public repo; nothing here
   is ever pushed to it. Tags and releases live on this private repo too — see
   **Cutting a release** below, and do that only when asked, like any other push.
7. **Commit messages and release notes are English** (they are git artifacts), even though
   replies to the user are 繁體中文.

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

`vendor/art-shuangpin/ime/SampleIME/MspyBridge.{h,cpp}` is the Windows equivalent of the
bridge this repo needs — **read it before changing `ArtBridge.mm`**, the two are deliberately
line-for-line comparable. `cli/repl.cpp` builds the same three layers:

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
| `core/` (composer, double pinyin, tone LM, user preferences) | 2272 | **zero Windows dependencies** |
| `engine/` (McBopomofo gramambular2 + LM) | 4419 | Windows code only in `MemoryMappedFile.{h,cpp}`, always behind `#ifdef _WIN32` with a POSIX `#else` |
| `ime/SampleIME/` (TSF shell) | 19090 | Windows-only — **replaced, not ported** |

v0.6 also added `cli/drill_gen.cpp`, `drills/` and ~3,000 lines of `web/` — a typing tutor.
None of it is input behaviour and none of it builds here; ignore it when reading a diff.

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

* **idle** `9`→←, `0`→→, `-`→Home, `=`→End, and **Tab→Backspace** (spec §6 「閒置導航鍵」,
  Tab added in v0.5). `-`/`=` are intercepted regardless of Shift; `9`/`0` are not
  intercepted when Shift is held (Shift+9 stays （); **Shift+Tab is never intercepted**,
  in either mode — reverse focus navigation is the safety valve for taking Tab away.
  While composing, Tab is simply a second `feedBackspace()`.
* **bare Shift tap** → `switchLanguage(toEnglish)` + toggle 中/英. Since v0.5 it
  **commits nothing** (spec §6 「中英切換」v5): the composition survives the switch and the
  composer inserts the separator space into its own buffer. The shell must never try to
  work out what is left of the caret — the passive last-character memory that used to do
  that (`ArtCharClass`, the commit-time caret snapshot, the global mouse monitor) was
  deleted with this change, and wanting it back means the change belongs elsewhere.
* **English-mode keys while a composition is live** →
  `-handleEnglishKeyDown:client:shift:`, the transliteration of upstream's
  `IsVirtualKeyNeedMspyEnglish`: Backspace/Tab delete, Enter commits, Esc clears, arrows
  are eaten, printable ASCII (Space included, case kept) goes to `feedEnglishChar`.
  With **nothing** composing, English mode still passes every key straight through — that
  is not an optimisation, it is the rule.
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

## Cutting a release

Releases live on the **private** `weiwei84530/art-shuangpin-mac`, tagged `vX.Y.Z` matching
`VERSION` (first one: `v0.2.0`, 2026-08-06). They exist for one job only: handing the user
`bootstrap.command` for a **first install or a recovery**. Ordinary updates never touch the
release page — the checked-out `~/ArtShuangpin/bootstrap.command` pulls and reinstalls itself.

Steps, in order. Never skip 2:

1. Bump `VERSION`.
2. **Bump `vendor.pin`** if upstream released — `ART_TAG`, `ART_ASSET`, `MSPY_DATA_SHA256`.
   The Mac fetches the *pinned tag*, so without this it keeps building the old core no matter
   what `tools/sync_art.py` has synced here. That script warns when the mirror has moved past
   the pin; the warning is the reminder.
3. Commit and push (only when asked).
4. `python tools/make_transfer_zip.py --bootstrap-only` → `dist/ArtShuangpin-bootstrap-<VERSION>.zip`.
5. `git tag -a vX.Y.Z -m "…"` and push the tag.
6. `gh release create vX.Y.Z --repo weiwei84530/art-shuangpin-mac --latest --notes-file … `
   with **both** assets: the zip *and* a plain copy of `bootstrap.command`.

**Why two assets, and why the zip is the one to use.** A `.command` served over HTTP arrives
with no Unix mode — browsers save it `0644` — so downloading the bare file and double-clicking
it reproduces exactly the *"could not be executed because you do not have appropriate access
privileges"* dialog. Only a ZIP carries the executable bit. The plain file is there for reading
and for anyone who would rather `chmod +x`. Release notes must also say **right-click → Open**,
not double-click: browser downloads are quarantined and this is not Developer ID signed.

The asset is a *snapshot* of `bootstrap.command` at that tag, so **re-upload it every release**
even when the script itself did not change — a stale asset would clone an older pin.

`--bootstrap-only` goes through the same `add()`/`verify()` path as the transfer archive, so
the `create_system = 3` fix and the CR check apply to it as well. Do not hand-roll the zip.

## Testing

There is no way to run this on Windows. What *can* be checked here:

* `vendor/art-shuangpin/` has a full C++ unit-test suite (`core/*_test.cpp`,
  `engine/**/*Test.cpp`) — that is the composer's coverage and it already passes upstream.
* `cli/repl.exe --keys "ni3hk3vs 99"` prints every state transition including `anchor:[...]`.
  (The space is load-bearing since v0.4: while `vs` is unsettled the digits are tone keys,
  not cursor keys.) `tools/artprobe.cpp` takes `~` for the bare-Shift switch, so
  `--keys "ni3hk3~ ok~"` replays the v0.5 中英 flow without a Mac.
  Use it to establish expected behaviour **before** writing shell code, so the Mac-side loop
  is only ever debugging the shell. `--user-choices <path>` loads a copy of the user's learned
  store so the output matches what they actually see; `tools/artprobe.cpp` takes the same flag
  on the Mac side. Since v0.6 those records are grid overrides, so they change what comes out,
  not merely how candidates rank — reproducing a report without the file reproduces a
  different engine.

The shell itself is verified by the user on the Mac. Keep it thin so there is little to
verify.
