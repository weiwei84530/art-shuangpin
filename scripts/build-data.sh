#!/bin/bash
# Builds out/data.txt from the vendored McBopomofo data sources (data/).
#
# The POSIX counterpart of scripts/build-data.ps1 -- same three steps in the
# same order with the same flags, and the two are meant to be read side by
# side. It exists because the macOS build cannot copy the language model out
# of a Windows build: out/ is gitignored, so the model is a build product
# that each host makes for itself.
#
#   bash scripts/build-data.sh
#
# Needs python3 and nothing else. The curation pipeline is 3.9+ with zero
# package dependencies, and Xcode Command Line Tools ship a python3, so a
# Mac that can compile the app can also build the dictionary.
set -euo pipefail

# python3, not python: macOS has shipped no unversioned `python` since 12.3
# and neither do the GitHub runner images. data/Makefile says python3 too.
# Overridable so this can be exercised on Windows, where it is python -- and
# it must produce the same bytes there, which is how it was checked in.
PYTHON="${PYTHON:-python3}"

# The curation scripts open files without an explicit encoding; force UTF-8
# rather than trust the platform default (cp950 on a zh-TW Windows box).
export PYTHONUTF8=1

root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/out"
mkdir -p "$out"
cd "$root/data"

# Step 1: PhraseFreq.txt from phrase.occ + exclusion.txt (writes into data/,
# where it is gitignored).
"$PYTHON" -m curation.builders.frequency_builder

# Step 2: data-raw.txt
"$PYTHON" -m curation.compilers.main_compiler \
    --heterophony1 heterophony1.list \
    --heterophony2 heterophony2.list \
    --heterophony3 heterophony3.list \
    --phrase_freq PhraseFreq.txt \
    --bpmf_mappings BPMFMappings.txt \
    --bpmf_base BPMFBase.txt \
    --punctuations BPMFPunctuations.txt \
    --symbols Symbols.txt \
    --macros Macros.txt \
    --output "$out/data-raw.txt"

# Step 3: data.txt
"$PYTHON" -m curation.compilers.postprocess \
    --input "$out/data-raw.txt" \
    --directive Postprocess.txt \
    --output "$out/data.txt"

# ParselessPhraseDB requires exact "\n" line endings, and Python writes CRLF
# in text mode on Windows. A no-op on macOS; kept because it is what the
# PowerShell side spends its last five lines on.
#
# Done in Python, not with grep/tr: MSYS grep strips CR before matching, so
# the obvious `if grep -q` guard silently never fires (measured -- it shipped
# a CRLF dictionary once), and `tr -d` would also eat a lone CR that was real
# data. This is exactly the PowerShell Replace("\r\n", "\n").
"$PYTHON" - "$out/data.txt" <<'NORMALIZE'
import io, sys
path = sys.argv[1]
data = io.open(path, 'rb').read()
if b'\r\n' in data:
    io.open(path, 'wb').write(data.replace(b'\r\n', b'\n'))
NORMALIZE

echo "OK: $out/data.txt"
