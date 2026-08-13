# ArtShuangpin for macOS — buildable with Command Line Tools alone.
#
# No .xcodeproj, no xcodebuild, no nib (ibtool ships with Xcode, not with
# CLT): plain clang++, a hand-assembled .app bundle, an ad-hoc signature.
# This file only ever runs on the Mac; the development host is Windows.
#
#   make            build dist/ArtShuangpin.app
#   make probe      build build/artprobe — exercises core+engine ONLY, no
#                   AppKit, no InputMethodKit.  Run this first when
#                   something is wrong: it tells you whether the problem is
#                   in the conversion engine or in the Cocoa shell.
#   make install    build, then install into ~/Library/Input Methods
#   make uninstall  remove it again
#   make clean      remove build/ and dist/
#
# Useful overrides:
#   make ARCH="arm64 x86_64"     universal build (default: this Mac's arch)
#   make MIN_MACOS=12.0          raise the deployment target
#   make V=1                     echo every command
#   make ICON_ASPECT=1.4         menu-bar icon width/height (default 1.375,
#                                which is 22x16 pt — measured off Apple's own
#                                ABC tile).  Redraws without a clean.
#   make install SIGN_ID="ArtShuangpin Dev"
#                                sign with a self-signed certificate instead
#                                of ad-hoc, so the Accessibility grant (idle
#                                9/0/-/=) survives a rebuild.  See
#                                docs/NOTES.md.

APP_NAME  := ArtShuangpin
BUNDLE_ID := com.mspy.inputmethod.ArtShuangpin
VERSION   := $(shell cat VERSION 2>/dev/null || echo 0.0.0)

# Localizations shipped in the bundle.  en is REQUIRED, not a translation:
# see the InfoPlist.strings rule below.
LPROJ := en zh-Hant

# Menu-bar icon width / height.  1.375 (= 22 x 16 pt) is measured off
# Apple's own ABC tile and Squirrel's; see tools/make_icon.m.
ICON_ASPECT ?= 1.375

# ---------------------------------------------------------------- layout ---

VENDOR     := vendor/art-shuangpin
CORE_DIR   := $(VENDOR)/core
ENGINE_DIR := $(VENDOR)/engine
DATA_FILE  := vendor/mspy-data.txt

BUILD    := build
DIST     := dist
APP      := $(DIST)/$(APP_NAME).app
CONTENTS := $(APP)/Contents
BIN_DIR  := $(CONTENTS)/MacOS
RES_DIR  := $(CONTENTS)/Resources

# ----------------------------------------------------------------- tools ---

CXX       ?= clang++
CC        ?= clang
SDK       := $(shell xcrun --show-sdk-path 2>/dev/null)
ARCH      ?= $(shell uname -m)
MIN_MACOS ?= 11.0

# Code signing identity.  "-" is ad-hoc, which needs no setup but makes TCC
# forget the Accessibility grant on every rebuild: an ad-hoc signature is
# identified by its code directory hash, and that changes whenever the binary
# does, so the tick left in System Settings belongs to the *previous* build
# and the idle navigation keys stop working.  Sign with a self-signed
# certificate instead and the grant survives rebuilds:
#
#   make install SIGN_ID="ArtShuangpin Dev"
#
# See docs/NOTES.md, "Ad-hoc signing and Accessibility", for how to create
# that certificate once.
SIGN_ID ?= -

ARCH_FLAGS := $(foreach a,$(ARCH),-arch $(a))
SYSROOT    := $(if $(SDK),-isysroot $(SDK),)

# No -fvisibility=hidden: IMK instantiates the controller class by name out
# of Info.plist, and there is no reason to make that lookup interesting.
COMMON_FLAGS := $(ARCH_FLAGS) $(SYSROOT) -mmacosx-version-min=$(MIN_MACOS) \
                -O2 -g -Wall -Wno-unused-parameter -MMD -MP
INCLUDES     := -I$(CORE_DIR) -I$(ENGINE_DIR) -I$(ENGINE_DIR)/mcbopomofo -Isrc

CXXFLAGS    := -std=c++20 $(COMMON_FLAGS) $(INCLUDES)
OBJCXXFLAGS := -std=c++20 $(COMMON_FLAGS) $(INCLUDES) -fobjc-arc

FRAMEWORKS := -framework Cocoa -framework InputMethodKit \
              -framework Carbon -framework ApplicationServices
LDFLAGS    := $(ARCH_FLAGS) $(SYSROOT) -mmacosx-version-min=$(MIN_MACOS)

ifeq ($(V),1)
Q :=
else
Q := @
endif

# --------------------------------------------------------------- sources ---

# Mirrors vendor/art-shuangpin/CMakeLists.txt.  Listed rather than globbed:
# the same directories hold *_test.cpp files that must not be linked in.
ENGINE_SRC := \
  $(ENGINE_DIR)/gramambular2/reading_grid.cpp \
  $(ENGINE_DIR)/mcbopomofo/AssociatedPhrasesV2.cpp \
  $(ENGINE_DIR)/mcbopomofo/ByteBlockBackedDictionary.cpp \
  $(ENGINE_DIR)/mcbopomofo/McBopomofoLM.cpp \
  $(ENGINE_DIR)/mcbopomofo/MemoryMappedFile.cpp \
  $(ENGINE_DIR)/mcbopomofo/ParselessLM.cpp \
  $(ENGINE_DIR)/mcbopomofo/ParselessPhraseDB.cpp \
  $(ENGINE_DIR)/mcbopomofo/PhraseReplacementMap.cpp \
  $(ENGINE_DIR)/mcbopomofo/UserOverrideModel.cpp \
  $(ENGINE_DIR)/mcbopomofo/UserPhrasesLM.cpp \
  $(ENGINE_DIR)/mcbopomofo/UTF8Helper.cpp

CORE_SRC := \
  $(CORE_DIR)/double_pinyin.cpp \
  $(CORE_DIR)/syllable_input.cpp \
  $(CORE_DIR)/relaxed_tone_lm.cpp \
  $(CORE_DIR)/user_preferences.cpp \
  $(CORE_DIR)/composer.cpp

SHELL_SRC := \
  src/main.mm \
  src/ArtBridge.mm \
  src/ArtInputController.mm \
  src/ArtCandidateWindow.mm \
  src/ArtModeHUD.mm \
  src/ArtNavigation.mm

CPP_OBJ   := $(ENGINE_SRC:%=$(BUILD)/%.o) $(CORE_SRC:%=$(BUILD)/%.o)
SHELL_OBJ := $(SHELL_SRC:%=$(BUILD)/%.o)
PROBE_OBJ := $(BUILD)/tools/artprobe.cpp.o

# ----------------------------------------------------------------- rules ---

.PHONY: all app probe install uninstall clean preflight help FORCE

# `all` must stay the first real target in this file: it is what a bare
# `make` builds.
all: app

# Prints the header block: line 2 through the first blank line, so adding to
# the comment above never silently truncates this.
help:
	@sed -n '2,/^$$/p' Makefile | sed 's/^# \{0,1\}//'

# The failure this build is actually likely to hit: vendor/ is gitignored,
# so a fresh checkout has neither the core sources nor the language model.
# Say so precisely instead of emitting 200 include errors.
preflight:
	$(Q)missing=""; \
	[ -f "$(CORE_DIR)/composer.cpp" ] || missing="$$missing\n  $(CORE_DIR)/  (the art-shuangpin source mirror)"; \
	[ -f "$(DATA_FILE)" ]             || missing="$$missing\n  $(DATA_FILE)  (the 7.5 MB language model)"; \
	if [ -n "$$missing" ]; then \
	  printf '\nvendor/ is incomplete — missing:%b\n\n' "$$missing"; \
	  printf 'vendor/ is gitignored, so it does not travel with a git clone.\n'; \
	  printf 'Copy the whole vendor/ directory over from the Windows host with\n'; \
	  printf 'something that does not rewrite line endings (mspy-data.txt needs LF).\n'; \
	  printf 'On the Windows host it is (re)created by:  python tools/sync_art.py\n\n'; \
	  exit 1; \
	fi

$(BUILD)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "  CXX    $<"
	$(Q)$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.mm.o: %.mm
	@mkdir -p $(dir $@)
	@echo "  OBJCXX $<"
	$(Q)$(CXX) $(OBJCXXFLAGS) -c $< -o $@

$(BUILD)/$(APP_NAME): $(CPP_OBJ) $(SHELL_OBJ)
	@mkdir -p $(dir $@)
	@echo "  LINK   $@"
	$(Q)$(CXX) $(LDFLAGS) $(FRAMEWORKS) $^ -o $@

# --- bundle -----------------------------------------------------------------

$(BIN_DIR)/$(APP_NAME): $(BUILD)/$(APP_NAME)
	@mkdir -p $(BIN_DIR)
	$(Q)cp -f $< $@

$(CONTENTS)/Info.plist: resources/Info.plist VERSION
	@mkdir -p $(CONTENTS)
	$(Q)sed -e 's/__VERSION__/$(VERSION)/g' \
	        -e 's/__BUNDLE_ID__/$(BUNDLE_ID)/g' \
	        -e 's/__APP_NAME__/$(APP_NAME)/g' \
	        -e 's/__MIN_MACOS__/$(MIN_MACOS)/g' $< > $@

$(CONTENTS)/PkgInfo:
	@mkdir -p $(CONTENTS)
	$(Q)printf 'APPL????' > $@

$(RES_DIR)/mspy-data.txt: $(DATA_FILE)
	@mkdir -p $(RES_DIR)
	@echo "  DATA   $@"
	$(Q)cp -f $< $@

# docs/INSTALL.md ships inside the bundle as README.txt (traditional
# Chinese, end-user facing).
$(RES_DIR)/README.txt: docs/INSTALL.md
	@mkdir -p $(RES_DIR)
	$(Q)cp -f $< $@

# Both localizations carry the same __BUNDLE_ID__ substitution as Info.plist:
# the input source's displayed name is keyed by the input MODE id, so a
# hand-written copy of the bundle id here would break the name silently the
# day the id changed.
#
# en.lproj is NOT optional and is not "the English translation". CFBundle
# resolves a localization from the user's UI language, falling back to
# CFBundleDevelopmentRegion (en); when nothing resolves, every lookup returns
# its own key, and the input source ends up named
# "com.mspy.inputmethod.ArtShuangpin.Chinese". See docs/NOTES.md.
$(RES_DIR)/%.lproj/InfoPlist.strings: resources/%.lproj/InfoPlist.strings
	@mkdir -p $(dir $@)
	$(Q)sed -e 's/__BUNDLE_ID__/$(BUNDLE_ID)/g' $< > $@

LPROJ_FILES := $(LPROJ:%=$(RES_DIR)/%.lproj/InfoPlist.strings)

$(BUILD)/make_icon: tools/make_icon.m
	@mkdir -p $(BUILD)
	@echo "  CC     $<"
	$(Q)$(CC) $(ARCH_FLAGS) $(SYSROOT) -mmacosx-version-min=$(MIN_MACOS) \
	    -fobjc-arc -framework Cocoa $< -o $@

# The menu-bar icon is generated rather than committed as a binary blob, so
# there is nothing opaque in the repo.  Deliberately failure-tolerant (the
# leading '-'): a missing icon costs a blank menu-bar glyph, not a build.
#
# Depends on .iconaspect as well, so `make ICON_ASPECT=1.4` redraws without a
# clean — same trick as .signid below.
$(RES_DIR)/$(APP_NAME).tiff: $(BUILD)/make_icon $(BUILD)/.iconaspect
	@mkdir -p $(RES_DIR)
	@echo "  ICON   $@  (aspect $(ICON_ASPECT))"
	-$(Q)$(BUILD)/make_icon $@ $(ICON_ASPECT)

$(BUILD)/.iconaspect: FORCE
	@mkdir -p $(dir $@)
	@[ "$$(cat $@ 2>/dev/null)" = "$(ICON_ASPECT)" ] || printf '%s' "$(ICON_ASPECT)" > $@

BUNDLE_FILES := $(BIN_DIR)/$(APP_NAME) $(CONTENTS)/Info.plist $(CONTENTS)/PkgInfo \
                $(RES_DIR)/mspy-data.txt $(RES_DIR)/README.txt \
                $(RES_DIR)/$(APP_NAME).tiff \
                $(LPROJ_FILES)

# Signature.  Locally built apps carry no quarantine attribute, so Gatekeeper
# never runs on them; this is here because an unsigned bundle is refused when
# the system loads it as an input method.
#
# Depends on .signid as well as the bundle, so that changing SIGN_ID re-signs
# an otherwise unchanged bundle.
$(BUILD)/.signed: $(BUNDLE_FILES) $(BUILD)/.signid
	@echo "  SIGN   $(APP)  ($(if $(filter -,$(SIGN_ID)),ad-hoc,$(SIGN_ID)))"
	$(Q)codesign --force --sign "$(SIGN_ID)" "$(APP)"
	$(Q)touch $@

# Rewritten only when the identity actually changes, so it does not force a
# re-sign on every build.
#
# FORCE is an always-stale prerequisite: it makes this recipe run every build,
# and the recipe then moves the timestamp only when SIGN_ID really changed.
# Defined here rather than near the top because the first real target in a
# Makefile is its default goal, and that must remain `all`.
FORCE:

$(BUILD)/.signid: FORCE
	@mkdir -p $(dir $@)
	@[ "$$(cat $@ 2>/dev/null)" = "$(SIGN_ID)" ] || printf '%s' "$(SIGN_ID)" > $@

app: preflight
	$(Q)$(MAKE) --no-print-directory $(BUILD)/.signed
	@echo "built $(APP)  ($(ARCH), min macOS $(MIN_MACOS))"

# --- the core-only probe ----------------------------------------------------

probe: preflight
	$(Q)$(MAKE) --no-print-directory $(BUILD)/artprobe
	@echo "built $(BUILD)/artprobe"
	@echo "try:  $(BUILD)/artprobe --data $(DATA_FILE) --keys 'ni3hk3vs#'   (# = Enter; 'vs' stays bopomofo until settled)"

$(BUILD)/artprobe: $(CPP_OBJ) $(PROBE_OBJ)
	@mkdir -p $(dir $@)
	@echo "  LINK   $@"
	$(Q)$(CXX) $(LDFLAGS) $^ -o $@

# --- install ----------------------------------------------------------------

# Invoked through bash rather than as ./scripts/install.sh: the repo is
# authored on Windows, where git cannot set the executable bit.
install: app
	$(Q)bash ./scripts/install.sh

uninstall:
	$(Q)bash ./scripts/uninstall.sh

clean:
	$(Q)rm -rf $(BUILD) $(DIST)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
