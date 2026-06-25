# Self-play strength measurement

A dependency-free harness for measuring GNU Go's playing strength by self-play.
Every strength change in the engine should be validated here: play one build
or parameter set against another and look at the Elo difference, not at
intuition.

## Files

- `twogtp.py` — a minimal two-engine GTP referee (stdlib only). Plays N games
  with alternating colors, scores each with the engines' own `final_score`,
  and reports the win split plus an Elo estimate with a 95% confidence band.
- `selfplay.sh` — convenience wrapper.

## Usage

```sh
# Current build vs a pinned reference build (the honest strength test):
./selfplay.sh \
  "../../interface/gnugo --mode gtp --level 1" \
  "/opt/gnugo-ref/gnugo --mode gtp --level 1" \
  40 9 7.5

# A/B test two parameter sets of the SAME binary via env overrides
# (see ../../engine/params.c for the tunable knobs):
GG=../../interface/gnugo
./selfplay.sh \
  "env GNUGO_TERRITORIAL_WEIGHT=1.2 $GG --mode gtp --level 1" \
  "$GG --mode gtp --level 1" \
  100 9
```

## Interpreting the output

```
A win rate: 57.5%
A Elo vs B: +52 +/- 38
```

A is ~52 Elo stronger than B, with a 95% confidence band of ±38. The band
shrinks as `1/sqrt(games)`, so for a confident verdict on a small change you
typically want 200–500 games. On 9x9 at level 1 a game takes well under a
second, so a few hundred games is a couple of minutes.

## Notes

- Color is alternated each game so first-move advantage cancels out.
- A build playing itself should score ~50% / Elo ~0 (sanity check).
- For a strength *regression* gate in CI, pin a reference binary and require
  the new build to be within noise (or better).
