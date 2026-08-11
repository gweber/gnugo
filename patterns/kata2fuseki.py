#!/usr/bin/env python3
"""kata2fuseki.py -- build a fuseki9.dbz from KataGo's published 9x9 book.

Reads the katagobooks.org HTML dump (book9x9tt: Tromp-Taylor, komi 7 --
exactly CGOS 9x9 rules) in ONE streaming pass over the tarball and emits
gnu go's compressed fuseki database format:

    F-H0-<n> <weight> <move><stones...>

where each coordinate is an sgf-style pair (column letter, row letter),
the FIRST pair is the move to play and the rest are the position's stones
in strictly alternating colors starting with X.  uncompress_fuseki expands
these into fullboard patterns; the matcher tries all 8 transformations and
maps colors by side to move, so each book page (already symmetry-canonical)
becomes exactly one pattern per accepted move.

Color convention (from fullboard_matchpat's color_map): X must be the
stones of the player NOT to move, O the player to move.  Pages where the
stone counts are inconsistent with that (an early capture happened) cannot
be expressed as an alternating list and are skipped -- rare in the opening.

Move acceptance per page: KataGo's wl is win-loss from White's side (the
page JS negates it for nextPla==1), so the mover's badness is
(nextPla==1 ? wl : -wl).  We accept moves within WL_MARGIN of the page's
best AND with enough visits, weight 5..100 by closeness -- gnugo picks
weighted-random among matched patterns, giving principled variety.

Usage:
    python3 kata2fuseki.py <book.tar.gz> > fuseki9.dbz.new
Progress goes to stderr.
"""
import json
import re
import sys
import tarfile

MAX_STONES = 12     # book depth: positions with at most this many stones
WL_MARGIN = 0.03    # accept moves this close to the page's best (wl units)
VISIT_FRAC = 0.02   # ... and with >= this fraction of the page's max 'av'
                    # ('av' = average visits; the 'v' field is a fixed-point
                    # encoding with wild magnitudes, only useful as > 0)
MAX_PATTERNS = 150000

BOARD_RE = re.compile(r"const board = \[([0-9,]*)\]")
NEXTPLA_RE = re.compile(r"const nextPla = (\d)")
MOVES_RE = re.compile(r"const moves = (\[.*?\]);", re.S)


def parse_page(text):
    b = BOARD_RE.search(text)
    n = NEXTPLA_RE.search(text)
    m = MOVES_RE.search(text)
    if not (b and n and m):
        return None
    board = [int(x) for x in b.group(1).split(",") if x != ""]
    if len(board) != 81:
        return None
    js = m.group(1).replace("'", '"')
    js = re.sub(r",\s*([}\]])", r"\1", js)   # strip trailing commas
    moves = json.loads(js)
    return board, int(n.group(1)), moves


def sgf(x, y):
    return chr(ord("a") + x) + chr(ord("a") + y)


def emit_lines(board, nextpla, moves, out):
    nb = sum(1 for v in board if v == 1)
    nw = sum(1 for v in board if v == 2)
    if nb + nw > MAX_STONES:
        return 0
    # X = not-to-move color's stones, O = to-move color's stones.
    if nextpla == 1:
        xs, os_ = 2, 1
    else:
        xs, os_ = 1, 2
    xpts = [(i % 9, i // 9) for i, v in enumerate(board) if v == xs]
    opts = [(i % 9, i // 9) for i, v in enumerate(board) if v == os_]
    # Strict alternation parity: black to move needs nb == nw, white to move
    # needs nb == nw + 1.  Anything else arose via a pass or a capture; pass
    # lines in particular would otherwise leak wrong-tempo recommendations
    # into normal-play positions (the empty board matches them trivially).
    if nextpla == 1:
        ok = nb == nw
    else:
        ok = nb == nw + 1
    if not ok or len(xpts) not in (len(opts), len(opts) + 1):
        return -1    # pass/capture position; not expressible as alternation
    stones = ""
    for k in range(len(xpts) + len(opts)):
        pt = xpts[k // 2] if k % 2 == 0 else opts[k // 2]
        stones += sgf(*pt)

    sign = 1.0 if nextpla == 1 else -1.0
    cands = []
    maxv = 0.0
    for mv in moves:
        if not mv.get("xy") or mv.get("v", 0) <= 0:
            continue
        x, y = mv["xy"][0]
        if not (0 <= x < 9 and 0 <= y < 9):
            continue
        if board[y * 9 + x] != 0:
            continue
        cands.append((sign * mv["wl"], float(mv.get("av", 0)), x, y))
        maxv = max(maxv, float(mv.get("av", 0)))
    if not cands:
        return 0
    best = min(c[0] for c in cands)
    n = 0
    for wl, v, x, y in cands:
        d = wl - best
        if d > WL_MARGIN or v < VISIT_FRAC * maxv:
            continue
        weight = max(5, int(round(100 * (1.0 - d / WL_MARGIN))))
        out.append((nb + nw, weight, sgf(x, y) + stones))
        n += 1
    return n


def main():
    tarpath = sys.argv[1]
    out = []
    seen = skipped_cap = pages = 0
    with tarfile.open(tarpath, "r:gz") as tf:
        for member in tf:
            if not member.name.endswith(".html"):
                continue
            if "/root/" not in member.name and not re.search(
                    r"/[0-9A-F]{2}/[0-9A-F]{64}\.html$", member.name):
                continue
            pages += 1
            if pages % 100000 == 0:
                print("scanned %d pages, kept %d lines" % (pages, len(out)),
                      file=sys.stderr)
            text = tf.extractfile(member).read().decode("utf-8", "replace")
            # cheap depth pre-filter before full parsing
            b = BOARD_RE.search(text)
            if not b:
                continue
            if sum(ch != "0" for ch in b.group(1).split(",")) > MAX_STONES:
                continue
            parsed = parse_page(text)
            if not parsed:
                continue
            r = emit_lines(*parsed, out)
            if r == -1:
                skipped_cap += 1
            elif r > 0:
                seen += 1

    # keep the shallowest / heaviest patterns if over budget
    out.sort(key=lambda t: (t[0], -t[1]))
    out = out[:MAX_PATTERNS]
    for i, (_, weight, stones) in enumerate(out):
        print("F-H0-%d %d %s" % (i + 1, weight, stones))
    print("pages=%d positions-used=%d lines=%d capture-skips=%d"
          % (pages, seen, len(out), skipped_cap), file=sys.stderr)


if __name__ == "__main__":
    main()
