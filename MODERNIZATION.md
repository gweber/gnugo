# Modernization notes

This repository is a copy of the official [GNU Go](https://www.gnu.org/software/gnugo/)
sources from [GNU Savannah](https://savannah.gnu.org/projects/gnugo/), with a
set of changes that let the engine build and run cleanly on a current
(2020s-era) toolchain while preserving its playing behavior.

The upstream history is unchanged; all new work lives on top of upstream commit
`84a32e9c` and is kept on the `modernize` branch. Savannah remains configured as
the `upstream` git remote.

## What changed

### Build fixes

- **`-fno-common` link failure.** GCC 10+ (and Clang) default to `-fno-common`,
  which turns the tentative definitions of `meaningless_black_moves[]` and
  `meaningless_white_moves[]` in `engine/liberty.h` into multiple-definition
  link errors. They are now declared `extern` in the header with a single
  definition in `engine/globals.c`, matching how `worm[]` and the other engine
  globals are handled. Without this fix the build does not link at all.

### Correctness fixes (found via compiler warnings)

- **Owl pattern VA32** (`patterns/owl_vital_apats.db`). The constraint
  `lib(a)>1 || !oplay_attack(*,?,b,*)==WIN` parses as
  `(!oplay_attack(...)) == WIN`. Since `!` yields 0 or 1 and `WIN == 5`, the
  right-hand side of the `||` was *always false*, so the pattern silently
  degraded to `lib(a)>1`. Rewritten to `... oplay_attack(*,?,b,*) != WIN`,
  which is the idiom used by every other constraint in the file and matches the
  author's evident intent.

- **`engine/breakin.c`.** `memset(non_territory, 0, BOARDMAX)` zeroed only
  `BOARDMAX` bytes of an `int[BOARDMAX]` array (a quarter of it on a 4-byte
  `int`). Uses `sizeof` now. (Benign in practice because the array is
  append-only, but the intent was clearly to clear the whole array.)

- **`interface/play_ascii.c`.** The `COMMENT` command ignored the result of
  `fgets`; on end-of-input the comment buffer was used uninitialized. It is now
  cleared when `fgets` fails.

### Warning cleanup (no behavior change)

Reduced warnings from **162 to 13** on GCC 13 with the project's default flags:

- `-Wformat-security` (9): data strings are passed through a `"%s"` format
  rather than as the format string itself (`dfa.c`, `uncompress_fuseki.c`,
  `mkpat.c`).
- `-Wexpansion-to-defined` (222): `USE_WINDOWS_SOCKET_CLUDGE` hid `defined`
  inside a macro body (undefined behavior when expanded). It is now computed as
  a plain `0`/`1` macro.
- `-Wunused-result` (4): `fread`/`write`/`fgets` return values are now checked
  (`main.c`, `gmp.c`, `play_ascii.c`).
- `-Wstringop-overflow` (2): `hane_rescue_moves`/`special_rescue5_moves`
  declared their `libs[]` parameter with a fixed size larger than some call-site
  arrays; the loops are bounded by `countlib(str)`, so the parameter is now
  declared unsized (semantically identical, silences the false positive).

The remaining 13 warnings are cosmetic (`-Wmisleading-indentation`) or live in
build-time code generators (`-Wstringop-truncation`), and are harmless.

### Infrastructure

- **Continuous integration** (`.github/workflows/ci.yml`): builds with both GCC
  and Clang on every push and pull request, then runs version, GTP, and owl
  life-and-death regression smoke tests.

## Building

```sh
./configure
make -j"$(nproc)"
./interface/gnugo --version
```

## Validation

The owl / life-and-death regression suites
(`regression/owl.tst`, `owl1.tst`, `ld_owl.tst`) — the ones exercising the
changed pattern — and a sample of general suites (`capture`, `blunder`,
`atari_atari`, `connection`, `nicklas1`, `9x9`, `13x13`) all run with **no
unexpected results and no crashes**. Run them yourself with:

```sh
cd regression
../interface/gnugo --quiet --mode gtp < owl.tst | awk -f regress.awk tst=owl.tst
```
