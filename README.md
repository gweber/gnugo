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

## The one idea worth knowing

Across many experiments, the measured verdict is that GNU Go's MC weakness is **whole-board
positional evaluation** — the thing that classically only a value network reaches. So the
durable gains here are on the **search-parallelism** axis, not on algorithm tweaks. Strength
is settled by **self-play, not intuition** ([`regression/selfplay/`](regression/selfplay/)):
`parmatch.py` plays an N-game A/B match between two env-var configs of the same binary and
reports the Elo delta.

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
| `GNUGO_MC_PRIOR` | `10.0` | Strength of the classical influence/shape move-value prior. |
| `GNUGO_MC_ROBUST` | `0` | Final root pick: `0` = highest winrate, `1` = robust child (most visits). |
| `GNUGO_MC_SHRINK` | `0` | Winrate shrinkage toward 0.5 for low-visit children. |

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

## Repository map

| Path | What's there |
|---|---|
| [`engine/montecarlo.c`](engine/montecarlo.c) | The MC engine — playouts, UCT/RAVE selection, tree/root parallelism, scoring, every knob above. |
| [`regression/selfplay/`](regression/selfplay/) | Self-play strength harness (`parmatch.py`, `twogtp.py`, tuning scripts). |
| [`patterns/`](patterns/) | Pattern databases + MC ML tooling (`mctrain`, `mcdiff`, `mccalib`, …). |
| [`MODERNIZATION.md`](MODERNIZATION.md) | Build/toolchain fixes for modern GCC/Clang, and the correctness bugs found along the way. |

## License

GNU Go is free software under the **GNU General Public License** (see [`COPYING`](COPYING)).
This fork inherits that license; all changes here are offered under the same terms.
