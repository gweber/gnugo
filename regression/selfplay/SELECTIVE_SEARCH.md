# Step 4 — Global selective search (prototype + measured result)

## Idea
GNU Go chooses its move from a *static* evaluation of move reasons; it has
almost no whole-board lookahead. `selective_engine.py` adds a one-ply
lookahead **without modifying the engine**: it wraps a real gnugo over GTP and
intercepts `genmove`. For each of gnugo's own top-`width` candidate moves
(`top_moves_<color>`) it plays the move, reads the whole-board score
(`estimate_score`), undoes it, and finally plays the candidate that leaves the
best resulting position. Everything else is forwarded verbatim.

This reuses GNU Go's own candidate generator and its own positional evaluation
as the search leaf — the simplest *sound* form of the "give it lookahead"
idea, and a scaffold for deeper search.

## Measured result (the point of having Step 0's harness)
Selective (width 5) vs plain gnugo, 9x9, level 1:

| games | selective win rate | Elo |
|-------|--------------------|-----|
| 20    | 65.0%              | +108 ± 178 |
| 60    | 48.3%              | **−12 ± 90** |

The encouraging 20-game number was small-sample noise. With enough games the
one-ply re-ranking is **strength-neutral**. Two reasons:
- `estimate_score` is *noisy in the opening/midgame* — the influence-based
  territory estimate of a position 1 ply ahead is not a reliable enough leaf
  signal to re-rank already-good candidates.
- GNU Go's move-reason valuation already encodes most of what a 1-ply static
  re-rank would add, so there is little new information.

This is a deliberately reported negative/neutral result: the mechanism is
sound and reusable, and the harness prevented shipping a non-improvement.

## Where this likely *does* help (future work)
- **Deeper search** (2–3 ply minimax over the top-k), where lookahead beats a
  single static re-rank — needs candidate-list snapshotting so the inner
  `genmove`/`top_moves` for replies don't clobber state.
- **Endgame only**, where `estimate_score` is accurate (small, settled
  boards) — gate the re-rank on move number / emptiness.
- **A tactical leaf** (combine with the df-pn ladder solver and reading) rather
  than the influence score alone.

## Usage
```sh
python3 twogtp.py \
  --black "python3 $(pwd)/selective_engine.py --gnugo \"../../interface/gnugo --mode gtp --level 1\" --width 5" \
  --white "../../interface/gnugo --mode gtp --level 1" \
  --games 60 --size 9
```
