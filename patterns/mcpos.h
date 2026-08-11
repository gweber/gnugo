/* mcpos.h -- shared glue for the standalone MC research tools
 * (mccalib, mcdiff, mcpredict; mctrain shares only the engine-init dance).
 *
 * Header-only on purpose: these tools are not part of the autotools build
 * because two of them need an out-of-tree oracle library.  See
 * build-mctools.sh next to this file for the exact, reproducible build.
 *
 * Position-file format (decode_positions.py):
 *   <target> <ctm> <nstones> <r c col>{nstones}       one line per position
 *
 * Row frame: r counts from the BOTTOM of the original (KataGo/pgx) game,
 * r = GTP row - 1 (see gtp2a_local in mushin-go's distill_gen.py).  Replaying
 * r as GNU Go's top-down row index mirrors the board vertically inside the
 * engine.  That is a harmless Go symmetry -- every consumer (playouts, dragon
 * analysis, the oracle) sees the same mirrored board -- but any coordinate
 * printed for a human must use the SOURCE frame: row = r + 1, col = 'A' + c
 * (skipping 'I').  Do not "fix" a printed r + 1 to board_size - r: that would
 * flip labels out of the frame the original games (and their SGF) live in.
 */
#ifndef MCPOS_H
#define MCPOS_H

#include <stdio.h>
#include <stdlib.h>

#include "gnugo.h"
#include "liberty.h"

/* saigo's sound L&D oracle (FFI; formerly tugo, long since merged into
 * saigo): per-point ownership with df-pn reading.  cells/out length n*n
 * row-major; 0=empty/unknown, 1=black, 2=white.  It only returns owners it
 * has PROVEN (Benson pass-alive + df-pn) and abstains on anything tempo-,
 * ko- or seki-dependent, so it needs no side to move.  Link against saigo's
 * target/release/libsaigo_net.so (see build-mctools.sh). */
extern void saigo_ownership_ld(const unsigned char *cells, size_t n,
			       unsigned long ld_budget, unsigned char *out);

/* Read the nstones stone records of one position onto the (cleared) board.
 * A short read, an out-of-range coordinate, a bad color or a duplicated
 * point is fatal: a partially loaded board would silently desync the stream
 * and corrupt every later position, so garbage input must never be able to
 * masquerade as a result. */
static inline void
mc_read_stones(FILE *f, int nstones, int posno)
{
  int i, r, c;
  char col[4];

  for (i = 0; i < nstones; i++) {
    if (fscanf(f, "%d %d %3s", &r, &c, col) != 3) {
      fprintf(stderr, "mcpos: malformed stone %d/%d in position %d\n",
	      i + 1, nstones, posno);
      exit(2);
    }
    if (r < 0 || r > 8 || c < 0 || c > 8
	|| (col[0] != 'B' && col[0] != 'W')
	|| board[POS(r, c)] != EMPTY) {
      fprintf(stderr, "mcpos: bad stone '%d %d %s' in position %d\n",
	      r, c, col, posno);
      exit(2);
    }
    add_stone(POS(r, c), (col[0] == 'B') ? BLACK : WHITE);
  }
}

/* Read one full position (header + stones); clears the 9x9 board itself.
 * Returns 1 with *target/ctm filled, 0 on clean EOF.  A malformed or
 * truncated header is fatal, so end-of-data and broken data are never
 * conflated.  posno is the 1-based number of the position being read. */
static inline int
mc_read_position(FILE *f, double *target, char ctm[4], int posno)
{
  int nstones;
  int ret = fscanf(f, "%lf %3s %d", target, ctm, &nstones);

  if (ret == EOF && !ferror(f))
    return 0;
  if (ret != 3 || nstones < 0 || nstones > 81) {
    fprintf(stderr, "mcpos: malformed header at position %d\n", posno);
    exit(2);
  }
  gnugo_clear_board(9);
  mc_read_stones(f, nstones, posno);
  return 1;
}

/* Copy the engine board into the oracle's 81-cell raster. */
static inline void
mc_board_to_cells(unsigned char cells[81])
{
  int r, c;

  for (r = 0; r < 9; r++)
    for (c = 0; c < 9; c++) {
      int b = board[POS(r, c)];
      cells[r * 9 + c] = (b == BLACK) ? 1 : (b == WHITE) ? 2 : 0;
    }
}

#endif /* MCPOS_H */
