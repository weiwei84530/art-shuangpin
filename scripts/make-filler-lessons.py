# -*- coding: utf-8 -*-
"""Writes the drill lessons that fill in whatever syllables the hand-written
ones miss.

The graded lessons in drills/lessons.txt are prose and only reach the
syllables prose happens to use -- about 59% of single-character usage. The
rest has to come from somewhere, so this picks real two-character words out
of the dictionary by greedy set cover: at each step, the word that covers
the most still-uncovered syllables wins, breaking ties by how common the
word is.

With --targets it aims at every syllable the keyboard can type (the list
drill_gen --audit walks), not at a usage threshold, so scripts\
check-drill-coverage.ps1 can hold the drills to covering the whole
keyboard. What it still cannot reach ends up in drills/skip-syllables.txt.

    python scripts/make-filler-lessons.py --data out/data.txt \
        --covered out/drill-syllables.txt --targets out/drill-reachable.txt \
        --out drills/filler.txt
"""

import argparse
import collections
import io
import math
import os

TONE_MARKS = ("ˊ", "ˇ", "ˋ", "˙")

# Below this score a dictionary entry is a word the learner will never meet;
# drilling it would teach the keys with an unreadable example. The rare
# syllables at the tail have nothing above the floor, so they get a second
# pass at FALLBACK_WORD_SCORE rather than going uncovered.
MIN_WORD_SCORE = -8.0
FALLBACK_WORD_SCORE = -13.0
# A word is only as readable as its rarest character. Measured against
# out/data.txt: the characters the user picked out as unreadable (耒 耨 欻
# 裒 煢 衲) all sit at -6.4 or below, while everything they were happy with
# (虐 咱 倆 唷 嗲 剖 僧 窮) is above -6.2. This floor holds in BOTH passes:
# a syllable with no readable word is better left out of the drills than
# taught with a character nobody can read.
MIN_CHAR_SCORE = -6.3
# Used only when --targets is not given: how much of single-character usage
# the drills should reach.
USAGE_TARGET = 0.99

WORDS_PER_SENTENCE = 5
# Exactly this many filler lessons, the words spread evenly between them:
# a fifth unit holding the last ten words is not worth its place in the list.
FILLER_LESSONS = 4


def bare(reading):
    for mark in TONE_MARKS:
        if reading.endswith(mark):
            return reading[: -len(mark)]
    return reading


def code_points(text):
    return list(text)


def load(path):
    """Returns (syllable mass, everyday two-character words, rare ones)."""
    mass = collections.Counter()
    best_char = collections.defaultdict(lambda: -99.0)
    # value -> list of (reading, score). A word with several readings is
    # kept: the lesson line spells the reading out (詞/ㄉㄨˊ-ㄧㄣ), so the
    # generator cannot disagree with us. Dropping them instead cost real
    # vocabulary -- 剖析 and 解剖 are only ambiguous between ㄆㄡ and ㄆㄡˇ,
    # and losing them left ㄆㄡ with nothing but 裒輯 to drill it with.
    by_value = collections.defaultdict(list)
    with io.open(path, encoding="utf-8") as handle:
        for line in handle:
            if not line.strip() or line.startswith("#"):
                continue
            fields = line.split()
            if len(fields) < 3 or "_" in fields[0]:
                continue
            reading, value = fields[0], fields[1]
            try:
                score = float(fields[2])
            except ValueError:
                continue
            syllables = reading.split("-")
            if len(code_points(value)) != len(syllables):
                continue
            if len(syllables) == 1:
                mass[bare(reading)] += math.exp(score)
                best_char[value] = max(best_char[value], score)
            by_value[value].append((reading, score))

    words, rare = [], []
    for value, readings in by_value.items():
        if len(value) != 2:
            continue
        if any(best_char[c] < MIN_CHAR_SCORE for c in code_points(value)):
            continue  # an unreadable character teaches the key with a puzzle
        reading, score = max(readings, key=lambda entry: entry[1])
        entry = (value, reading, reading.split("-"), score)
        if score >= MIN_WORD_SCORE:
            words.append(entry)
        elif score >= FALLBACK_WORD_SCORE:
            rare.append(entry)
    return mass, words, rare


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", default="out/data.txt")
    parser.add_argument("--covered", default="out/drill-syllables.txt")
    parser.add_argument("--out", default="drills/filler.txt")
    parser.add_argument("--exclude", default=None,
                        help="words the walk gets wrong; never pick these")
    parser.add_argument("--targets", default=None,
                        help="syllables to cover, one per line (from "
                             "drill_gen --reachable); without it, the "
                             "commonest %d%% of usage" % round(USAGE_TARGET * 100))
    args = parser.parse_args()

    mass, words, rare = load(args.data)

    # Words the generator has already caught the sentence walk getting
    # wrong. The drill never corrects anything, so a word that does not
    # convert cleanly simply cannot be used.
    banned = set()
    if args.exclude and os.path.exists(args.exclude):
        # utf-8-sig, and strip stray marks: the file is appended to round by
        # round and Windows tools like to prepend a BOM each time.
        with io.open(args.exclude, encoding="utf-8-sig") as handle:
            banned = {line.replace("﻿", "").strip() for line in handle}
        banned.discard("")
    if banned:
        words = [w for w in words if w[0] not in banned]
        rare = [w for w in rare if w[0] not in banned]

    covered = set()
    if os.path.exists(args.covered):
        with io.open(args.covered, encoding="utf-8") as handle:
            covered = {line.strip() for line in handle if line.strip()}

    # What to cover: everything the keyboard can type when the audit hands
    # us its list, otherwise the common end of the distribution.
    total = sum(mass.values())
    if args.targets and os.path.exists(args.targets):
        with io.open(args.targets, encoding="utf-8") as handle:
            wanted = {line.strip() for line in handle if line.strip()}
    else:
        running = 0.0
        wanted = set()
        for syllable, weight in mass.most_common():
            wanted.add(syllable)
            running += weight
            if running / total >= USAGE_TARGET:
                break
    todo = wanted - covered

    chosen = []

    def cover(pool):
        """Greedy set cover: the word that reaches the most of what is left
        wins, ties going to the commoner word."""
        remaining = list(pool)
        while todo:
            best = None
            best_gain = 0
            best_score = -1e9
            for entry in remaining:
                # DISTINCT syllables: 煢煢 covers ㄑㄩㄥ once, not twice, and
                # counting it twice let it beat 貧窮.
                gain = len({bare(r) for r in entry[2]} & todo)
                if gain > best_gain or (gain == best_gain and gain > 0 and
                                        entry[3] > best_score):
                    best, best_gain, best_score = entry, gain, entry[3]
            if best is None:
                return  # nothing left in this pool can reach the rest
            chosen.append(best)
            remaining.remove(best)
            for reading in best[2]:
                todo.discard(bare(reading))

    cover(words)
    if todo:
        # Whatever is left has no everyday word; take the best that exists
        # rather than leaving the key combination undrilled.
        cover(rare)

    lines = [
        "// Generated by scripts/make-filler-lessons.py -- do not edit.",
        "// Two-character dictionary words picked by greedy set cover, one",
        "// per key combination the graded lessons miss. The reading is",
        "// spelled out because several of them have more than one.",
        "",
    ]
    per_lesson = max(1, -(-len(chosen) // FILLER_LESSONS))  # ceil, evenly
    for index in range(0, len(chosen), per_lesson):
        block = chosen[index:index + per_lesson]
        number = index // per_lesson + 1
        lines.append("#id=filler%d" % number)
        lines.append("#title=補完 %d：全鍵位掃描" % number)
        lines.append(
            "#intro=這一課不是文章，是為了把前面沒練到的注音組合補齊而從詞庫挑出來的常用詞。"
            "每行五個詞，中間有一個逗號，打完一整行才按 Enter：一次上屏，一次換行。")
        for start in range(0, len(block), WORDS_PER_SENTENCE):
            row = ["%s/%s" % (word, reading)
                   for word, reading, _, _ in block[start:start + WORDS_PER_SENTENCE]]
            # A comma part way through: the drill only presses Enter at a
            # full stop, so this is where the comma key gets practised.
            head, tail = row[:2], row[2:]
            text = " ".join(head)
            if tail:
                text += " ， " + " ".join(tail)
            lines.append(text + " 。")
        lines.append("")

    with io.open(args.out, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))

    reached = covered | {bare(r) for _, _, readings, _ in chosen for r in readings}
    usage = sum(w for s, w in mass.items() if s in reached) / total
    print("filler words: %d in %d lessons" % (len(chosen),
                                              (len(chosen) + per_lesson - 1) //
                                              per_lesson))
    print("syllables reachable: %d/%d (%.2f%% of usage)" %
          (len(reached & set(mass)), len(mass), 100.0 * usage))
    if todo:
        print("still uncovered (%d): %s" % (len(todo), " ".join(sorted(todo))))
    if banned:
        print("excluded %d word(s) the walk gets wrong" % len(banned))
    print("wrote %s" % args.out)


if __name__ == "__main__":
    main()
