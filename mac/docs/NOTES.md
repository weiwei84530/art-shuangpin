# Implementation notes

Working notes for the macOS shell. `CLAUDE.md` has the rules and the
measured facts; this file has the decisions taken while writing `src/`, and
— more usefully — where to look first when something is wrong.

The behaviour authority is `vendor/art-shuangpin/docs/spec.md` §6. Nothing
here overrides it.

## The shape of the thing

```
main.mm                 IMKServer, and the --install self-registration path
ArtBridge.{h,mm}        the only file that contains C++: owns
                        McBopomofoLM -> RelaxedToneLM -> mspy::Composer,
                        republishes it as Cocoa types, and persists the
                        store of learned corrections
ArtInputController.mm   IMKInputController: key routing, marked text,
                        Chinese/English mode, the IMK menu
ArtCandidateWindow.mm   NSPanel + custom NSView, positioned under the anchor
ArtModeHUD.mm           the 中/英 flash
ArtNavigation.mm        CGEventPost, Accessibility trust
tools/artprobe.cpp      core-only CLI (`make probe`)
tools/make_icon.m       build-time menu-bar icon generator
*.command               double-clickable wrappers around make
```

## Getting a working tree onto the Mac without carrying one

`vendor/` is gitignored and never reaches a remote, which used to mean a clone
was not buildable and a 2.7 MB archive had to be hand-carried every round.
That was never actually necessary: **neither half of `vendor/` is private.**

| `vendor/` | source | credentials |
|---|---|---|
| `art-shuangpin/` | the public `weiwei84530/art-shuangpin` repo, at a tag | none |
| `mspy-data.txt` | the public release asset for that tag | none |
| the shell itself | the private `weiwei84530/art-shuangpin-mac` | **yes** |

The language model looked like the blocker, since upstream's `out/` is
gitignored and rebuilding it means running the whole `data/` Python pipeline.
It is not: the Windows install package published with every release carries
it, and the copy inside `art-shuangpin-v0.5.zip` hashes to exactly the
`MSPY_DATA_SHA256` this repo has been pinning all along. Verified before
relying on it, and re-verified at every bump — v0.5 rebuilt no `data/`, so
the model is byte-identical to v0.4's and only `ART_TAG` / `ART_ASSET` moved.

So `bootstrap.command` is one self-contained file that clones, reconstructs
`vendor/`, and hands over to `install.command`. Run from inside a checkout it
updates that checkout instead of cloning, which makes it the update path too —
double-click it in `~/ArtShuangpin` and there is nothing left to transfer.

The pinned coordinates live in `vendor.pin`, deliberately `KEY=value` with no
spaces so bash can `source` it and a five-line Python parser can read it.
`tools/sync_art.py` reads the same file, so the hash the Windows side accepts
and the hash the Mac downloads cannot drift apart. That script follows the
source repo's HEAD while the bootstrap fetches a TAG, so it now says out loud
when this tree has moved past the release the Mac would get.

**Bump `vendor.pin` when tracking a new upstream release**, or the Mac keeps
building the old one no matter what is synced here.

## The .command wrappers

`bootstrap.command`, `install.command`, `check-engine.command` and
`uninstall.command` exist so the Mac side needs no Terminal. A `.command` file
*is* a shell script — Finder just runs it in Terminal instead of opening it in
an editor — so there is nothing new to maintain, only three traps that are all
handled and all worth knowing:

* **Finder starts them in `$HOME`**, not in the folder they live in. Every one
  of them opens with `cd "$(dirname "$0")"`.
* **The executable bit has to survive the trip from Windows.** Git on this
  host cannot set it, so `tools/make_transfer_zip.py` stores 0755 for `.sh`
  and `.command` entries in the archive — which is how the Mac actually
  receives them. Without it, a double-click opens the file in TextEdit;
  `START-HERE.txt` says so and gives the `chmod +x` line.
* **A single CR breaks them** with `bad interpreter: /bin/bash^M`, which reads
  as file corruption rather than as a line-ending problem. `.gitattributes`
  pins `*.command` to LF, and the packer now *refuses to build an archive*
  containing a CR in any shipped shell script.

Two deliberate choices inside them:

* **No `set -e`.** Every failure path has to reach the `read` at the end, or
  Terminal closes over the error message before it can be read.
* **`install.command` always cleans first.** This is the path for someone who
  does not want to think about the build, and a stale object file linking an
  old input core back in looks exactly like "the update did not work". A
  minute of rebuild is the cheaper failure.

It also picks up the `ArtShuangpin Dev` signing identity automatically when
`security find-identity` reports one, so the Accessibility grant stops dying
on every install without the user having to remember a `SIGN_ID=` argument.

**`install.command` resets the Accessibility grant — but only when signing
ad-hoc, and that condition is the whole point.** An ad-hoc signature changes
the code directory hash every build, so the row still sitting in System
Settings is already dead and clearing it costs nothing; the reset is what
makes the permission prompt fire again instead of the keys quietly doing
nothing. With the certificate the grant *survives* the rebuild, and resetting
would revoke a working permission and demand re-approval for no reason.

`tccutil reset Accessibility` may or may not need root depending on the macOS
version, so it runs as the user first and escalates to `sudo` only if that
fails — and because a `.command` runs in an interactive Terminal, `sudo` can
prompt for the password there rather than failing silently. This is also why
`killall ArtShuangpin` follows it: the process re-checks its trust state only
on launch.

Their user-facing output is 繁體中文, like `docs/INSTALL.md` and for the same
reason: they are read by the person installing, not by whoever is editing the
repo. The comments in them are English like everything else.

Everything the composer decides stays in the composer. The shell owns the
idle navigation keys (`9`/`0`/`-`/`=`, and Tab since v0.5), the bare-Shift
toggle plus the per-application memory of what it set, the numpad exemption,
"arrows do nothing while composing", routing English-mode keys into the same
composition, and drawing. That list is complete; if something outside it
shows up here, it is a bug.

## Tracking upstream

`vendor/` is refreshed with `python tools/sync_art.py`; the mirror is at
**v0.6**. (The source repository moved from `D:\Claude\Input` to
`D:\Projects\art-shuangpin` on 2026-08-06 — the sync script re-points an older
mirror's fetch URL by itself, so this needed no manual repair.)

**`vendor/` is gitignored, so it does not travel with the commit.** Most of
what a release changes is core, which means pulling this repo on the Mac
without re-copying `vendor/` gets none of it and the fixes look absent. Copy
the whole directory across (LF endings — `mspy-data.txt` needs them) and
`make clean`, or the stale object files link the old composer straight back
in.

Most upstream releases need nothing here, and it is worth knowing why before
reaching for the editor: behaviour lives in `mspy::Composer` and
`ArtBridge.mm` is a pure passthrough, so a core change reaches the shell the
moment the mirror moves. `core/composer.h`'s public surface did not change in
v0.3 at all — everything was private.

### v0.3 — one shell change out of five

| upstream change | shell work |
|---|---|
| the syllable in progress stays bopomofo (`hk` shows ㄏㄠ, not 蒿) | none — `displaySegments().unconfirmed` |
| Space settles instead of committing | none — `feedChar(' ')` |
| picking a candidate parks the cursor past the fixed span | none — `selectCandidate()` |
| Chinese/English remembered per application | **all of it** — see below |
| candidate window scaled by monitor DPI | none — a GDI problem. AppKit is point-based and Retina is automatic; `ArtCandidateWindow.mm` has no pixel constants to fix |

One shape worth noticing in the Space behaviour: it commonly returns
*consumed* with an **empty** `commitText` and leaves the composer composing.
`-syncWithCommit:client:` already handles that — it only calls `insertText:`
for a non-empty commit — but a future key handler written on the assumption
that "consumed implies committed" would be wrong. v0.4 makes that shape the
common case rather than the exception.

### v0.4 — one shell change again, but this time it is the bridge

| upstream change | shell work |
|---|---|
| unsettled window widened: a tone digit no longer settles the syllable either (`zv7` shows ㄗㄨㄟˋ) | none — `displaySegments()` |
| right-hand mirrored tone digits (`0`=1 `9`=2 `8`=3 `7`=4 `6`=5) | none — `wouldConsume`/`feedChar` |
| digits are tone keys *or* control keys depending on whether anything is unsettled | none — same |
| Backspace un-tones before it deletes | none — `feedBackspace()` |
| punctuation settles and JOINS the composition instead of committing it | none — but see below |
| `Composer`'s constructor now takes the outermost `LanguageModel`, not `RelaxedToneLM` | one line |
| the score-0 user-phrase store replaced by weighted, decaying preferences | **all of it** — see below |

The punctuation change needs no code, but it does change what the shell is
responsible for. Nothing auto-commits any more: a composition can run for a
whole paragraph, and the only things that produce text are Enter, Space with
nothing left to settle, a Shift language switch, a NumPad key, and losing
focus. `-deactivateServer:` calling `-commitCompositionInto:` is therefore
load-bearing in a way it was not before — if it ever cancelled instead of
committing, a user clicking away would lose a paragraph rather than a word.

### v0.5 — two shell changes, and one deletion larger than both

| upstream change | shell work |
|---|---|
| a tone digit SETTLES the syllable again (`hk3` shows 好), so the control keys come back at once | none — `displaySegments()` / `wouldConsume` |
| no more un-toning: Backspace deletes the whole syllable | none — `feedBackspace()` |
| lone first keys are syllables (字 = `z4`, 知 = `v` + Space) | none — `feedChar()` |
| a key pair the dictionary rejects is eaten instead of shown as fake bopomofo (`hy` = ㄏㄨㄞ, not ㄏㄩ) | none — `feedChar()` |
| **Tab = Backspace** | **shell** — composing it is `feedBackspace()`; idle it injects a real Backspace |
| **the Shift switch no longer commits; English joins the composition** | **shell + bridge** — `switchLanguage()`, `feedEnglishChar()` |

The last row is the one with teeth. English mode used to mean "pass every key
through", so `-handleKeyDown:client:` could return `NO` two lines in and be
done. Now a *live composition* survives the switch, so those keys have to be
eaten and routed into the same buffer: that is
`-handleEnglishKeyDown:client:shift:`, the transliteration of upstream's
`IsVirtualKeyNeedMspyEnglish`. With nothing composing the old behaviour is
preserved exactly, and that is deliberate — ordinary English typing must not
start passing through an input method that has no reason to see it.

**What came out is bigger than what went in.** The whole passive
last-character memory — `ArtCharClass`, the caret snapshot taken at commit
time, the global mouse monitor that invalidated both, `-observeBypassedKey:`
threaded through six call sites — existed to guess which character sat to the
left of the caret in a document the input method cannot read. v5 puts both
sides of that junction inside the composer's own text, so `switchLanguage()`
decides it exactly and none of that machinery has anything left to do. About
90 lines left `ArtInputController.mm`. Do not bring them back: the guesswork
was never better than "usually right".

One consequence worth carrying while reading the file — `-syncWithCommit:
client:` is still the only place that calls `insertText:`, but the Shift tap
no longer reaches it with anything to insert. A language switch commits
nothing at all now, not even the separator space, which goes *into* the
composition instead.

### v0.6 — a big release upstream, one file here

Most of v0.6 is a typing tutor: `cli/drill_gen.cpp`, `drills/`, and about
3,000 lines of `web/`. None of it is input behaviour and none of it ships in
this app. What reaches the shell is the learning rewrite, and it lands
entirely inside `ArtBridge.{h,mm}`.

| upstream change | shell work |
|---|---|
| **every** letter key is a single-key syllable (`d`+Space = 的, `n`+`4` = 訥, `j` = ㄐㄧ) | none — `feedChar()` |
| picking a candidate no longer re-words the rest of the sentence | none — `selectCandidate()` |
| `ㄕㄤ` moves hands, and the tone key after it | none — `feedChar()` |
| **`UserPreferenceLM` deleted**; `Composer` takes `RelaxedToneLM` again and holds the store itself | **bridge** — see below |
| **`onManualSelection` + `onPhraseUsed` → `onLearned`** | **bridge** |
| **the store gained a context field, so the file changed name** | **bridge** |

The single-key row is worth one sentence of attention even though it costs no
code: `n`+Space used to settle as the bopomofo symbol ㄋ and now produces 呢,
so a user who typed bare ㄋ deliberately needs `` n` ``. That is upstream's
call, documented in spec §5, and the shell has no say in it.

## The preference store (v0.6)

`ArtBridge.mm` is a port of upstream's `CMspyBridge::LoadPreferences` /
`SavePreferences`; keep the two readable side by side.

What changed and why is in `core/user_preferences.h` and spec §7 — read those
rather than this, but the shape matters here because it moved a
responsibility out of the bridge. A record now answers *"right after
&lt;context&gt;, the reading &lt;X&gt; means &lt;value&gt;"*, and the composer
applies it as a **high-score node override on the reading grid**. Learning is
not a language model layer any more, which is why `UserPreferenceLM` is gone
and `Composer` takes `RelaxedToneLM` directly again.

That is the third design in as many releases, and the reason each was
replaced is worth keeping straight, because two of them look like they ought
to have worked:

1. The original fed the store to McBopomofo's `UserPhrasesLM`, which scores
   every entry at exactly 0 while dictionary scores are negative log
   probabilities — so *any* learned phrase beat the dictionary, and the
   earliest line in the file won its reading forever. `wo3vidk4` produced
   我知道 with no learning and 我之道 with it. **`McBopomofoLM::loadUserPhrases`
   is still deliberately never called**; deleting that call was half the fix.
2. v0.4's weighted layer fixed "cannot be corrected" but scored a learned
   phrase only 1e-6 above the best entry for the *same reading*. That wins
   the reading and loses the walk: 不鏽鋼(-5)+悲(-3) beats
   不(-2)+鏽(-3)+鋼杯(-6.8) however the last term is nudged, so correcting 悲
   to 杯 changed nothing the next time round. Widening the margin distorts
   span competition instead. Upstream measured both, in the REPL.

An override does not compete on score at all, so one correction is enough —
which is the whole user-visible point of the release.

The macOS side of the port, and where it deviates:

* **The file changed name with the format.**
  `~/Library/Application Support/ArtShuangpin/user-choices.txt`. A
  `user-phrases.txt` left by an earlier build records **no context**, and
  context is the whole of a record — it cannot be invented, so the old file
  is renamed to `user-phrases.txt.bak` and the new store starts empty.
  Upstream does the same, for the same reason. Parking rather than deleting
  matters: it is the only copy of what the user taught the old build.
* **`MoveFileEx` becomes `std::rename`.** Same guarantee, same directory, so
  a crash or a reader never sees a half-written store.
* **The merge-before-write is kept even though it is nearly redundant here.**
  On Windows every application hosts its own TIP instance with its own copy,
  so folding in the on-disk file is mandatory. One macOS process serves
  everybody, so there is usually no sibling to race — but a second copy of
  the input method, or the user editing the file by hand, costs nothing to
  survive.
* **Every write is now worth making immediately.** The v0.4 split — record
  now, throttle the usage refreshes — went out with the refreshes themselves:
  the new store has no time decay, so nothing needs touching merely to stay
  alive. `onLearned` fires only on a deliberate correction, which is rare and
  must survive a crash, so it saves on the spot. `kSaveThrottleSeconds` and
  `_lastSaveTime` are deleted.
* **The destructor becomes a notification.** Upstream flushes in
  `~CMspyBridge`; `ArtBridge` is a `dispatch_once` singleton that never
  deallocates, so the flush hangs off
  `NSApplicationWillTerminateNotification`. Best-effort by nature — an input
  method is usually ended with a signal — and it now covers only a failed
  write, since nothing is ever being held back.

`make probe` takes `--user-choices <path>`, renamed with the file and still
mirroring upstream's `repl`. Point it at the real store and the probe
produces what the running app produces, which is the only way to reproduce a
"why did it pick that" report offline. This is a stronger claim than it was:
the records now change the *output*, not merely the candidate order.

```sh
build/artprobe --data vendor/mspy-data.txt \
  --user-choices ~/Library/Application\ Support/ArtShuangpin/user-choices.txt \
  --keys "wo3vidk4"
```

## Key routing

`-[ArtInputController handleKeyDown:client:]` is a transliteration of
`CCompositionProcessorEngine::IsVirtualKeyNeedMspy`
(`ime/SampleIME/CompositionProcessorEngine.cpp:1597`), in the same order.
The order is load-bearing in one place: `wouldConsume()` returns true for
*every* idle digit and punctuation mark (digits are banned in Chinese mode
rather than typed literally, spec §6), so the idle navigation keys must be
taken off the table before it is asked. Reordering those two blocks silently
kills `9`/`0`/`-`/`=`.

Two macOS-specific details in the same function:

* Key *identity* for navigation comes from `charactersIgnoringModifiers`
  (Shift+9 still reads as `9`, which is what lets "do not intercept 9/0 while
  Shift is held" be expressed, leaving （） typable), while the character fed
  to the composer comes from `characters` (Shift+9 must arrive as `(` so the
  composer can convert it to （).
* The numpad exemption is decided from key codes, never from
  `NSEventModifierFlagNumericPad` — macOS sets that flag on the arrow keys
  too.

Since v0.5 there is a second, much shorter routing function beside it.
`-handleEnglishKeyDown:client:shift:` runs only when the mode is English
*and* something is composing, and it mirrors
`CCompositionProcessorEngine::IsVirtualKeyNeedMspyEnglish`: Backspace and Tab
delete, Enter commits, Esc clears, arrows are eaten so the caret cannot leave
the buffer, printable ASCII joins it with its case intact, and everything
else — function keys, anything carrying Cmd/Ctrl/Opt — belongs to the
application. Space counts as printable on purpose: it is a literal space
there, which is what makes a multi-word English run possible inside one
composition.

Tab is worth one more line. In Chinese mode it is a second Backspace while
composing and an *injected* Backspace while idle — the only idle navigation
key that deletes rather than moves. Shift+Tab is never intercepted in either
mode; reverse focus navigation is the safety valve for having taken Tab
away.

## Deliberate differences from the Windows build

**`-` / `=` post Cmd+Left / Cmd+Right, not Home / End.** The spec asks for
行首/行尾. On macOS, Home and End mean document start/end and in many apps
only scroll; line start/end is Cmd+arrow. The meaning is ported, not the key
code. A physically held Shift is carried onto the injected event, so
Shift+`-` extends the selection to the start of the line exactly as
Shift+Home does on Windows.

**The Chinese/English indicator is a HUD plus a menu checkmark, not a
menu-bar icon.** English mode on Windows is the system's own keyboard-open
compartment, so the OS toolbar shows 中/英 for free. The macOS equivalent is
declaring two input modes in `Info.plist` and calling
`-[IMKTextInput selectInputMode:]`. That is the right long-term answer, but
a wrong `tsInputModeListKey` makes the input method disappear from the input
source list altogether — a much worse failure than a missing indicator, and
not verifiable from the Windows development host. So v1 shows the state where
the user is already looking (a card that flashes by the caret) and in the IMK
menu. See "v2 upgrades" below.

**The candidate window is hand-rolled.** `IMKCandidates` cannot do the 1-6
digit labels, the page indicator, or — the part that matters — open under one
specific character rather than at the start of the composition.

## The caret and the anchor

`displaySegments().highlighted` is the anchor: the single character to the
right of the cursor. Together with the cursor position, that is two things to
communicate, and macOS gives an input method exactly one range plus a set of
attributes to do it with.

The first version spent the range on the anchor —
`selectionRange` = the anchor's range — expecting the host to highlight it and
put the caret at its start. **Measured on the Mac: it did neither.** The
caret stayed at the end of the composition while `9`/`0` moved the cursor
correctly underneath (proved by the candidate window, which opened at the
right character all along). Nothing visible moved.

What replaced it, one signal per channel:

| signal | channel |
|---|---|
| cursor position | `selectionRange`, **zero length**, at the cursor |
| the anchor | `NSUnderlineStyleThick` over that one character |
| the anchor, again | `NSUnderlineColorAttributeName` (accent) and `NSBackgroundColorAttributeName` |
| segment boundaries | `NSMarkedClauseSegmentAttributeName`, three clauses |

A thick underline on the active segment is macOS's own idiom — it is how
Japanese input marks the clause being worked on — so hosts understand it
without being asked to do anything unusual.

Two traps worth keeping written down:

* **The underline must be solid.** Spec §6 asks for a dotted one, which is
  what TSF draws on Windows. Chromium-based hosts (Chrome, Electron, LINE)
  reduce the whole attribute to `[style intValue] > 1`, and
  `NSUnderlineStyleSingle | NSUnderlinePatternDot` is 257 — every character
  then reads as thick and the anchor cannot be told from the rest. Solid thin
  plus solid thick is the only combination that survives.
* **`NSBackgroundColorAttributeName` is not in NSTextView's
  `validAttributesForMarkedText`.** It costs nothing to send and some hosts
  do use it, but it cannot be the only way the anchor is shown.

`-activateServer:` logs the host's bundle ID and its
`validAttributesForMarkedText` under the debug default. That is the fastest
way to find out what a misbehaving application will and will not draw.

If some host honours neither the selection range nor the attributes, the
remaining option is to draw the indicator ourselves: the anchor character's
screen rectangle is already known and correct — it is what positions the
candidate window — so a small overlay panel could mark it. Not built, because
so far it has not been needed.

The candidate window's position comes from
`attributesForCharacterIndex:lineHeightRectangle:` with the anchor's index in
the marked text. Hosts that report nothing get an empty rect, and the panel
then stays where it last was — deliberately, because jumping to a screen
corner reads as a bug while a slightly stale position does not.

## What to check first, in order of likelihood

1. **The input source does not appear in System Settings.**
   `resources/Info.plist`, specifically `ComponentInputModeDict` /
   `tsInputModeListKey`. Compare against a working third-party input method:
   `plutil -p "/Library/Input Methods/<something>.app/Contents/Info.plist"`.
   Log out and back in before concluding anything; the input source list is
   cached. `InputMethodConnectionName` must also match the string passed to
   `IMKServer` in `main.mm`.

2. **It is listed, selected, and typing produces plain letters.**
   The language model did not load. The IMK menu says so, and Console.app has
   the path it tried. Fail-open is intentional (see `-handleKeyDown:`): a
   broken dictionary must never cost the ability to type.

3. **Nothing appears at all, or the process is not running.**
   `log show --last 5m --predicate 'process == "ArtShuangpin"'`.
   A crash in `+[ArtBridge shared]` would show here. The engine is exercised
   independently by `make probe`, which is the fastest way to rule it out.

4. **The candidate panel never shows.** It is an `NSPanel` from an
   `LSBackgroundOnly` process. That combination is what every macOS input
   method uses and is expected to work; if it turns out not to, swap
   `LSBackgroundOnly` for `LSUIElement` in `Info.plist` — that is the one
   plist key worth trying blind.

5. **Idle 9/0/-/= or Tab do nothing.** Accessibility trust. The menu item
   reports the current state, and the first press prompts. A row sitting in
   System Settings — even a ticked one — proves nothing; if pressing `9`
   still raises the permission prompt, it is stale. See "Ad-hoc signing and
   Accessibility" below, which is the usual answer. Note that idle Tab is
   *eaten* either way, so without the grant it neither deletes nor moves
   focus — same shape as the other four.

6. **The input source name shows as the raw mode ID, or a menu header reads
   `CFBundleName`.** A localization lookup fell through — see "The name and
   the icon in the input menu" below. It is not a keying mistake; both
   symptoms come from the bundle having no `.lproj` matching the user's UI
   language.

## The name and the icon in the input menu

Both were wrong at once, and the diagnosis for each is worth keeping because
neither is guessable from the symptom.

### The name: one missing directory, two symptoms

On an English-language Mac the input menu showed the input source as

```
com.mspy.inputmethod.ArtShuangpin.Chinese     <- the mode ID, verbatim
CFBundleName                                  <- the menu header, verbatim
```

Both are `-[NSBundle localizedStringForKey:...]` returning **the key itself**,
which is what it does when no localization resolves. The bundle shipped only
`zh-Hant.lproj`; CFBundle picks a localization from the user's UI language and
falls back to `CFBundleDevelopmentRegion`, which is `en` — and there was no
`en.lproj`. Every `InfoPlist.strings` lookup therefore fell through.

So `en.lproj/InfoPlist.strings` is **required**, and it is not "the English
translation": it holds the same Chinese strings, because the input method has
one name in any UI language. `zh-Hant.lproj` stays for a Mac whose UI is
already Chinese. Keep the two in step.

Two details that make this fail quietly if changed:

* The picker entry is keyed by the **input mode ID**
  (`<bundle id>.Chinese`), not by `CFBundleName` and not by the bundle ID.
  Both `.strings` files go through the same `__BUNDLE_ID__` substitution as
  `Info.plist` so the two can never drift apart.
* UTF-8 with no BOM. Modern macOS reads UTF-8 `.strings`; the classic tooling
  wanted UTF-16, and half the advice online still says so.

**The system caches the name AND the icon, and it caches them somewhere the
install cannot reach.** The distinctive symptom, measured off one screenshot
taken right after a successful install:

| drawn by | our icon | our name |
|---|---|---|
| System Settings → Input Sources | 44 × 32 px = 22 × 16 pt — the new one | 阿特輸入法 |
| the menu bar and its input menu | 30 × 30 px = 15 × 15 pt — the OLD one | the raw mode ID |

One bundle, two renderers, opposite answers: System Settings re-reads the
bundle, the menu bar extra serves a cache. Our own section of that menu (the
中文/英文 items, the version line) was simultaneously correct and showing the
new version number, which is what rules out "the old binary is still running".

So when only the menu bar looks wrong, nothing is broken and nothing in this
repo needs changing. In escalating order: `killall ArtShuangpin`; remove and
re-add the input source; **log out and back in**, which always works because
the agent is relaunched and the input-source database re-scanned at login.

To confirm the installed bundle really is right before chasing anything:

```sh
ls "$HOME/Library/Input Methods/ArtShuangpin.app/Contents/Resources"   # en.lproj?
sips -g pixelWidth -g pixelHeight \
     "$HOME/Library/Input Methods/ArtShuangpin.app/Contents/Resources/ArtShuangpin.tiff"
```

44 × 32 pixels (the @2x rep) means the new icon is installed and only the
cache is stale.

### The icon: measured, not guessed

The icon looked wrong next to Apple's because it was both square and a point
short. Measured off a @2x screenshot of the input menu with all three listed
together:

| tile | pixels @2x | points | aspect |
|---|---|---|---|
| ABC | 44 × 32 | 22 × 16 | 1.375 |
| Squirrel | 44 × 32 | 22 × 16 | 1.375 |
| this, before | 30 × 30 | 15 × 15 | 1.0 |

Apple's tiles are **full bleed** — they measure the whole 44 × 32, with no
inset — and their corners reach full width about 3 px (@2x) in, so the radius
is roughly 1.75 pt on a 16 pt tile. `tools/make_icon.m` now draws to those
numbers: 16 pt tall (do not raise it, the menu bar scales it back down and it
only gets blurry), width from `ICON_ASPECT`, card filling the canvas.

`ICON_ASPECT` is a Make variable rather than a constant so the shape can be
nudged on the Mac without coming back to the source:

```sh
make install ICON_ASPECT=1.4
```

The `.iconaspect` stamp file makes that redraw without a `make clean`, the
same trick `SIGN_ID` uses below.

## Ad-hoc signing and Accessibility

`codesign -s -` produces a different code directory hash on every build, and
TCC (the permission database) keys Accessibility grants on exactly that. So
each `make install` invalidates the Accessibility grant, and the symptom is
the idle navigation keys quietly stopping.

**The trap is that the grant still looks present.** System Settings → Privacy
& Security → Accessibility keys its *list* on the bundle path, so the row
stays there, ticked, pointing at an entry whose signature no longer matches
the installed binary. Everything looks granted and `AXIsProcessTrusted()`
returns false. The tell is that pressing `9` re-triggers the permission
prompt: the prompt only fires when the process is untrusted, so a prompt plus
a ticked row means exactly this. `-[ArtNavigation inject:shiftHeld:]` now
says so in `NSLog` the first time it happens, no debug flag needed.

Clearing it:

```sh
tccutil reset Accessibility com.mspy.inputmethod.ArtShuangpin
```

then press `9` once and grant the prompt. Removing the row by hand with the
`−` button does the same thing.

**The way to stop it recurring** is to stop the signature changing: sign with
a self-signed certificate rather than ad-hoc, and TCC's requirement becomes
"this certificate" instead of "this exact binary", which survives rebuilds.
Once, in Keychain Access:

1. Keychain Access → Certificate Assistant → Create a Certificate…
2. Name `ArtShuangpin Dev`, Identity Type **Self Signed Root**, Certificate
   Type **Code Signing**. Create.

Then build with it, and re-grant Accessibility one final time:

```sh
make install SIGN_ID="ArtShuangpin Dev"
```

`SIGN_ID` defaults to `-` (ad-hoc), and the signing rule depends on a stamp
file holding its value, so switching identities re-signs without a
`make clean`. *This certificate path has not been exercised from the Windows
development host — it cannot be. If `codesign` rejects the identity, the
ad-hoc default plus `tccutil` above still works, it is just manual.*

None of this affects distribution: Gatekeeper never runs on locally built,
unquarantined apps, and input methods do not need a Developer ID (Squirrel
ships the same way).

## Testing

`vendor/art-shuangpin` carries the composer's real test suite (143 tests,
green upstream) and it is not rebuilt here — this repo has no behaviour to
test, only wiring.

`cli/repl.cpp` upstream cannot be built on macOS: it includes `<windows.h>`,
and `vendor/` is a read-only mirror that must not be patched. `tools/
artprobe.cpp` is the local stand-in — same idea, same output shape, no
Windows headers:

```sh
make probe
build/artprobe --data vendor/mspy-data.txt --keys "ni3hk3vs 99"
```

Establish what the composer *should* do there before debugging the shell.
Control tokens inside `--keys`: `<` Backspace (which is also what Tab does),
`!` Esc, `#` Enter, and `~` for the bare-Shift language switch — everything
after a `~` is fed through `feedEnglishChar` until the next one, so

```sh
build/artprobe --data vendor/mspy-data.txt --keys "ni3hk3~ ok~"
```

reproduces the v0.5 flow of 你好 ␣ok␣ living in one uncommitted buffer.

Debug logging in the shell:

```sh
defaults write com.mspy.inputmethod.ArtShuangpin debug -bool YES
killall ArtShuangpin
```

## Concurrency and lifetime

IMK creates one `IMKInputController` per client, so the composer and the
Chinese/English mode are process-wide statics rather than instance state — only one client has focus at a time, and
`-deactivateServer:` commits and hides before the next one activates. The
mode is a static in two parts: a per-application dictionary, plus a cache of
the focused application's value refreshed in `-activateServer:`. All IMK
callbacks arrive on the main thread, which is also the only thread that
touches AppKit here. There is no locking and there should not need to be.

`ArtBridge` is a process-lifetime singleton; the `onLearned` callback
captures it `__unsafe_unretained` on purpose (no cycle, no ARC machinery
inside a C++ lambda).

## Separator space — and the machinery it no longer needs (v0.5)

The half-width space between a Chinese run and an English one is decided
inside `Composer::switchLanguage()`, which reads the character to the left of
its **own** cursor. There is nothing left for the shell to do: it calls
`-switchLanguageToEnglish:` and renders what comes back.

This deserves a paragraph for what it replaced. Spec §6's v4 rule needed to
know what sat to the left of the caret *in the host's document*, which most
hosts will not tell an input method — so the shell kept a passive memory of
it: the class of the last character it committed itself, plus the class of
every key it watched pass through in English mode, plus a caret snapshot to
check that memory was still valid, plus a global mouse monitor to throw the
whole thing away on any click. Ported, measured, and it worked — "usually
right", with the failure direction deliberately set to a missing space rather
than a spurious one (寧缺勿濫).

All of it is gone. The moment English lives *inside* the composition, both
sides of the junction belong to the composer and the answer is exact. If a
future change makes the shell want to know what is left of the caret again,
that is the signal that the change is in the wrong place.

## Per-application Chinese/English mode

Spec §6 「中英模式各應用程式獨立記憶」, ported rather than copied. On Windows
the feature costs almost nothing: a TSF text service runs *inside* the
application's own process, so the object already **is** the per-application
slot, and the whole implementation is "re-assert my own value when focus
returns", because the system keeps a shared keyboard open/close state that
other applications move out from under it.

A macOS input method is the opposite shape — one process serving every
application — so the slot has to be explicit: `sModeByApp`, keyed by the
client's `bundleIdentifier`, with `sChineseMode` a cache of the focused
application's value, refreshed in `-activateServer:`. There is no shared
system state to fight here, so there is nothing to re-assert; reading the
dictionary is the entire restore path.

Three decisions worth keeping written down:

* **Applications start in English**, matching upstream's
  `InitializeSampleIMECompartment(FALSE)`. This is a real change in feel —
  the previous default was Chinese everywhere — and it is intended.
* **The memory dies with the application.**
  `NSWorkspaceDidTerminateApplicationNotification` drops the entry, so
  relaunching an application starts it in English again. Windows gets this
  for free (the text service dies with the process), and spec §6 says
  行程, not "bundle".
* **Menu switches are remembered, unlike on Windows.** Upstream excludes its
  language-bar toggle because it cannot tell that click apart from the
  system's own writes to the shared compartment. Our menu is our own and
  there is no such ambiguity, so it counts exactly like a Shift tap.

Restoring is silent — no HUD. The HUD is feedback for a switch the user just
made; flashing it on every application change would be noise.

## v2 upgrades, in the order they are worth doing

1. Two input modes (`...Chinese` / `...Roman`) in `ComponentInputModeDict`
   plus `-[IMKTextInput selectInputMode:]`, so the menu-bar icon itself flips
   between 中 and 英. Do this only once the plist is known good. This is worth
   more now than it was: with the mode remembered per application and every
   application starting in English, "which mode am I in" is asked more often,
   and the answer currently lives only in the IMK menu's checkmark.
2. Candidate panel polish: the spec's white rounded card with the pale-blue
   first row is approximated with semantic `NSColor`s so dark mode is not
   broken; the exact metrics are in the constants at the top of
   `ArtCandidateWindow.mm`.
