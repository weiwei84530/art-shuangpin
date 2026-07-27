"""Adds relaxed-tone alias keys for neutral-tone phrases to data.txt.

Spec (docs/spec.md, 2026-07-27 decision): a multi-syllable phrase that
contains at least one neutral-tone (˙) syllable gets an alias key with the
tone marks (ˊˇˋ) stripped from its OTHER syllables, e.g.

    ㄦˊ-ㄗ˙ 兒子  ->  alias ㄦ-ㄗ˙ 兒子

Typing the phrase with no tone digits expands each bare syllable to
{unmarked, ˙}, so the alias makes 兒子/什麼/爺爺 reachable without tones,
while phrases without neutral syllables (中國) keep the strict semantics.
Explicitly typed tone digits never match the stripped syllables, so tone
filtering still works.

Usage: python add_neutral_phrase_aliases.py <data.txt>   (rewrites in place)
"""

import sys

NEUTRAL = "˙"          # ˙
STRIP = "ˊˇˋ"  # ˊ ˇ ˋ


def main(path: str) -> None:
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()

    header = []
    body = lines
    if lines and lines[0].startswith("#"):
        header = [lines[0]]
        body = lines[1:]

    # (key, value) -> best score, over originals and aliases.
    rows = {}

    def add(key: str, value: str, score: float) -> None:
        prev = rows.get((key, value))
        if prev is None or score > prev:
            rows[(key, value)] = score

    aliases = 0
    for line in body:
        if not line:
            continue
        parts = line.split(" ")
        if len(parts) != 3:
            # Unexpected row shape: keep verbatim under a sentinel score.
            key, value, score_text = parts[0], " ".join(parts[1:-1]), parts[-1]
        else:
            key, value, score_text = parts
        score = float(score_text)
        add(key, value, score)

        if "-" in key and NEUTRAL in key:
            alias = key
            for mark in STRIP:
                alias = alias.replace(mark, "")
            if alias != key:
                add(alias, value, score)
                aliases += 1

    # UTF-8 byte order equals code-point order, so a plain sort keeps the
    # file valid for ParselessPhraseDB's binary search.
    out = header + [
        f"{key} {value} {score:.8f}"
        for (key, value), score in sorted(rows.items())
    ]
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")

    print(f"aliases added: {aliases}, total rows: {len(rows)}")


if __name__ == "__main__":
    main(sys.argv[1])
