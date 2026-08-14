<div align="center">

# GNU Go — Track B

**A research fork of [GNU Go](https://www.gnu.org/software/gnugo/): the classic engine, modernized to build on a current toolchain, with an experimental Monte-Carlo tree-search engine layered on top.**

`classic Go engine`  ·  `no neural network`  ·  `parallel MCTS`  ·  `measured by self-play`

</div>

---

GNU Go is a mature, knowledge-rich Go program with no neural network. This fork keeps all
of that intact and adds two things:

1. **Modernization** — the upstream sources build and run cleanly on a 2020s toolchain,
   with playing behavior preserved. See **[MODERNIZATION.md](MODERNIZATION.md)**.
2. **Track B** — an experimental **Monte-Carlo tree-search engine**
   ([`engine/montecarlo.c`](engine/montecarlo.c)), where the research on this branch lives.

Everything new is an **opt-in environment-variable knob**. With all of them unset, the
engine is **byte-for-byte the original GNU Go**.

## Quickstart

```sh
./configure && make

# One genmove via GTP — Monte-Carlo, 16-way tree-parallel search:
echo -e "boardsize 9\nclear_board\ngenmove black\nquit" \
  | GNUGO_MC_TREE_THREADS=16 ./interface/gnugo \
      --mode gtp --monte-carlo --mc-games-per-level 1000 --level 8
```

Upstream usage is unchanged — see the original [`README`](README), `gnugo --help`, and the
Texinfo manual under [`doc/`](doc/).

## The two ideas worth knowing

**1. The weakness is whole-board positional evaluation** — the thing that classically only a
value network reaches. In-search algorithm tweaks (backup operators, playout heuristics,
selection variants) are a measured dead zone; the durable gains are **search parallelism**
(~+237 Elo) and the **engine shell** — opening book, time management, pondering.

**2. GNU Go's own classical knowledge is worth ~100 Elo to the MC search** — and *how* you
inject it matters more than the fact that you do. Disabling the classical coupling entirely
costs ~98 Elo; ~78 of that came from a crude end-override that simply replaced the search's
choice with the classically best well-visited move. Feeding the same classical valuation in
as a **value term the search weighs continuously** (`GNUGO_MC_CLASSICAL_ROOT`) beats that
override by **+87 Elo**. The 30-year-old knowledge base isn't ballast — it just needs to
inform the search rather than veto it.

Strength is settled by **self-play, not intuition** ([`regression/selfplay/`](regression/selfplay/)):
`sprt.py` runs a sequential A/B test with common random numbers between two env-var configs
of the same binary and stops as soon as the result is decided; `parmatch.py` plays fixed-N
matches. Every Elo figure in this README comes from one of them.

> **Watch out:** never rebuild the binary in-tree while a match is running — the match execs
> `interface/gnugo` per game, so a mid-run `make` kills every remaining game. Copy the binary
> somewhere stable and point the match at the copy.

## Knobs

### 🧵 Parallelism — the real lever

| Env var | Default | Meaning |
|---|:---:|---|
| `GNUGO_MC_TREE_THREADS` | `1` (≤64) | **Tree parallelism** — N workers share one tree with virtual loss, pooling information. The big win (~**+237 Elo** vs root-parallel in earlier measurement). |
| `GNUGO_MC_THREADS` | `1` (≤64) | Root parallelism — N independent searches combined at the root. Weaker than tree-parallel. |
| `GNUGO_MC_VLOSS` | `1.0` | Virtual-loss weight — lost visits a descending thread counts as. |
| `GNUGO_MC_WU` | `0` | **WU-UCT** (Liu et al., ICLR 2020) — in-flight sims discount only the *confidence* term, not the *value*. Tree-parallel only. |

### 🎯 Selection policy

| Env var | Default | Meaning |
|---|:---:|---|
| `GNUGO_RAVE` | off | RAVE / MC-RAVE all-moves-as-first selection (Gelly & Silver). |
| `GNUGO_GRAVE` / `GNUGO_GRAVE_REF` | off | GRAVE — borrow a same-colour ancestor's AMAF once a node has `GRAVE_REF` playouts. |
| `GNUGO_MC_PRIOR` | `10.0` | Strength of the classical influence/shape move-value prior (worth ~20 Elo). |
| `GNUGO_MC_ROBUST` | `0` | Final root pick: `0` = highest winrate, `1` = robust child (most visits). |
| `GNUGO_MC_SHRINK` | `0` | Winrate shrinkage toward 0.5 for low-visit children. |
| `GNUGO_MC_LCB` | `0` (off) | **LCB root selection** (Leela Zero / KataGo) — final move by lower confidence bound on the *raw* winrate, among children with ≥10 % of max visits. **+19 Elo** at `1.28`. |
| `GNUGO_MC_CLASSICAL_ROOT` | `0` (off) | **Classical evaluation as a root value term** ("implicit-minimax lite") — blend normalized `potential_moves` into root children's selection value. **+87 Elo** at `0.15` when it *replaces* the end-override (with `GNUGO_MC_NO_OVERRIDE=1`). Sharp optimum: `0.08` → −58, `0.30` → −87. |
| `GNUGO_MC_NO_OVERRIDE` | `0` | Skip the classical end-override of the search's choice. Pairs with the knob above. |
| `GNUGO_MC_SCOREUTIL` | `0` (off) | **Score-utility tiebreak** (KataGo, no net needed) — add `ε`·mean score margin (mover's perspective) to a child's value, so among near-equal winrates the search prefers lines that also hold points. **+63 Elo** at `0.003`. |

### ⏱️ Tree reuse, pondering & search-time management

Wall-clock levers. GNU Go's GTP `genmove` already runs `adjust_level_offset()` off the
client's `time_left`, and the MC simulation budget scales with the level *every* move — so
`--level N --min-level A --max-level B` is a semi-dynamic clock schedule, and the knobs below
shift budget between moves within it.

| Env var | Default | Meaning |
|---|:---:|---|
| `GNUGO_MC_REUSE` | `0` | **Tree reuse** — keep the subtree under the played move and graft it into the next search (history-keyed, board-hash verified, so undo/new game simply miss). ~Neutral at fixed sims; it is the prerequisite for pondering. |
| `GNUGO_MC_PONDER` | `0` | **Pondering** — search the opponent's clock in a background thread, stopped by a GTP-read hook before any command is interpreted; harvested through tree reuse (implies it). **+72 Elo** with the opponent thinking as long as we do. |
| `GNUGO_MC_EARLYSTOP` | `0` (off) | **EARLY-C** — stop once the most-visited root child's lead exceeds `margin` × remaining sims *and* it has the better winrate: no selection policy can still flip. Loss-free at `1.0`; banks clock for later moves. |
| `GNUGO_MC_UNST` | `0` (off) | **UNST** — if the decision is *unstable* at budget's end (most-visited ≠ best-winrate, or runner-up within 85 % of the visits), extend the search once by `frac` × budget. The complement of early stop. |

### 🎲 Playout & scoring

| Env var | Default | Meaning |
|---|:---:|---|
| `GNUGO_MC_MERCY` | `0` | Mercy rule — abort a playout once one side leads by this many points (komi-aware). |
| `GNUGO_MC_AVOID_SELFATARI` | off | Skip self-atari moves in the playout policy. |
| `GNUGO_MC_LGRF` / `_P` | off | Last-good-reply-with-forgetting heuristic (probability `_P`). |
| `GNUGO_MC_ATARI` / `_P` | off | Atari-response heuristic (probability `_P`). |
| `GNUGO_MC_LD` | off | Benson pass-alive ownership in terminal scoring. |

### 🧬 Folding-inspired experiments — default off, expected small

Cross-domain ideas from protein-structure prediction — a globally-coupled system with local
structure, optimised by Monte-Carlo (the same shape as Go). Tree-parallel only.

| Env var | Default | Meaning |
|---|:---:|---|
| `GNUGO_MC_TEMPER` | off | **Parallel tempering** — spread worker UCT exploration over a geometric temperature ladder `[1/S, S]` (set `S`). Hot workers explore, cold ones refine. |
| `GNUGO_MC_CRIT` | off | **Criticality** (Coulom) — add `α`·(per-point ownership × outcome covariance) to a move's value, biasing search toward the decisive region. A whole-board *statistical* signal, not a hard board partition. |
| `GNUGO_MC_MSM` | off | **Markov-state-model value sharing** (the long shot) — pool win-rates over coarse phase × material node clusters, blend into low-visit nodes. |

### 🪦 Honest dead-ends

Kept as documented, default-off knobs so the negative result stays reproducible:
`GNUGO_MC_MAXENT` (MENTS/Tsallis soft-max value backup — strongly negative, **−128…−159 Elo**)
and `GNUGO_MC_VALUES` (learned value blend). Neither is a lever.

## The 9x9 opening book

The even-game 9x9 fuseki database ([`patterns/fuseki9.dbz`](patterns/fuseki9.dbz)) is
**generated from KataGo's published 9x9 opening book** (`book9x9tt` from
[katagobooks.org](https://katagobooks.org/), which is computed for Tromp-Taylor rules at
komi 7 — exactly the CGOS 9x9 setting). It replaces the historical even-game book;
the original handicap (`F-H2-*`) lines are preserved verbatim, and the 13x13, 19x19 and
joseki databases are untouched.

[`patterns/kata2fuseki.py`](patterns/kata2fuseki.py) streams the 5.55 M-page HTML dump once
and emits GNU Go's compressed fuseki format: 450 k patterns covering every book position
through 9 stones plus the best-visited ones at 10, keeping moves within 0.03 win-loss of
KataGo's choice and weighting them 5–100 by closeness (GNU Go then picks weighted-random
among matches, which gives principled opening variety rather than one fixed line).

Measured: **+60 Elo** for the book itself, **+19 Elo** more for deepening it from 8 to 10
plies. On a clock the gain compounds — book moves cost no thinking time, so the autolevel
schedule reinvests it in the middlegame.

> **The runtime stays neural-net-free.** The book is *offline-authored* by a stronger engine,
> the same way [`patterns/mctrain`](patterns/) fits playout weights against KataGo-labeled
> positions. Nothing in the playing engine evaluates a network.

**Licensing:** the upstream book is © 2026 David J Wu ("lightvector") under the **MIT**
license, which is GPL-compatible. Since `fuseki9.dbz` is a derived work, MIT's notice
requirement is met by [`patterns/FUSEKI9-KATAGO-LICENSE.txt`](patterns/FUSEKI9-KATAGO-LICENSE.txt),
which ships alongside it. To regenerate, rebuild from a fresh dump with the script; to opt
out entirely, restore the original `fuseki9.dbz` from Git history.

## Playing on CGOS

[`cgos/`](cgos/) deploys this fork on the 9x9 [Computer Go Server](http://www.yss-aya.com/cgos/)
— wrapper, reconnect loop, graceful stop, and a rating ladder of weaker rungs that fills the
otherwise empty band around the bot and doubles as external measurement. Ratings observed
there (still accumulating, and a moving target):

| Bot | Configuration | CGOS rating |
|---|---|---:|
| `saigo.gnugo3.9.1b` | full MC stack — book, pondering, 8 threads, LCB, autolevel | ~2350+ |
| `saigo.gnugo3.9.1` | pre-book MC stack, 4 threads, no pondering | ~2257 |
| `saigo.gnugoclassic` | **classical** engine, level 10, tuned weights | ~2106 |
| `Gnugo-3.7.10-a1` | stock GNU Go (the long-standing CGOS anchor) | 1800 |

So the classical engine alone is ~**+300 Elo** over stock GNU Go, and the MC stack adds
roughly another ~250 on top of that.

## Repository map

| Path | What's there |
|---|---|
| [`engine/montecarlo.c`](engine/montecarlo.c) | The MC engine — playouts, UCT/RAVE selection, tree/root parallelism, scoring, every knob above. |
| [`regression/selfplay/`](regression/selfplay/) | Self-play strength harness (`parmatch.py`, `twogtp.py`, tuning scripts). |
| [`patterns/`](patterns/) | Pattern databases (incl. the KataGo-derived 9x9 book + its `kata2fuseki.py` generator) and MC ML tooling (`mctrain`, `mcdiff`, `mccalib`, …). |
| [`cgos/`](cgos/) | CGOS deployment — bot wrapper, runner, graceful stop, rating ladder. |
| [`MODERNIZATION.md`](MODERNIZATION.md) | Build/toolchain fixes for modern GCC/Clang, and the correctness bugs found along the way. |

## License

GNU Go is free software under the **GNU General Public License** (see [`COPYING`](COPYING)).
This fork inherits that license; all changes here are offered under the same terms.

One bundled data file has separate upstream terms: the even-game 9x9 fuseki database is
derived from KataGo's opening book, © 2026 David J Wu ("lightvector"), MIT-licensed — see
[`patterns/FUSEKI9-KATAGO-LICENSE.txt`](patterns/FUSEKI9-KATAGO-LICENSE.txt). MIT is
GPL-compatible, so the combined work remains distributable under the GPL.
