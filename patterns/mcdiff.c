/* Differential test: GNU Go's heuristic dragon/owl life-and-death vs tugo's SOUND
 * df-pn oracle.  For each decoded position, compare GNU Go's dragon_status of
 * every stone against tugo's PROVEN ownership of that point.  tugo only returns a
 * verdict it has proven (Benson pass-alive + df-pn) and abstains otherwise, so any
 * stone where tugo proves an owner that contradicts GNU Go's definite ALIVE/DEAD
 * is a GNU Go bug -- tugo can't be the wrong one.
 *
 * Usage: mcdiff <positions.txt> [ld_budget]   (decode_positions.py format)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gnugo.h"
#include "liberty.h"
#include "patterns.h"

extern void tugo_ownership_ld(const unsigned char *cells, size_t n,
			      unsigned long ld_budget, unsigned char *out);

int
main(int argc, char **argv)
{
  FILE *f;
  char ctm[4], col[4];
  double target;
  int nstones, r, c, i;
  unsigned long budget = (argc > 2) ? strtoul(argv[2], NULL, 10) : 100000;
  int ndisagree = 0, nproven = 0, npos = 0;
  unsigned char cells[81], own[81];

  if (argc < 2) {
    fprintf(stderr, "usage: %s <positions.txt> [ld_budget]\n", argv[0]);
    return 1;
  }
  init_gnugo(20.0, 1);
  gnugo_clear_board(9);
  komi = 7.5;

  f = fopen(argv[1], "r");
  if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }

  while (fscanf(f, "%lf %3s %d", &target, ctm, &nstones) == 3) {
    int rr, cc;
    gnugo_clear_board(9);
    for (i = 0; i < nstones; i++) {
      if (fscanf(f, "%d %d %3s", &r, &c, col) != 3)
	break;
      add_stone(POS(r, c), (col[0] == 'B') ? BLACK : WHITE);
    }
    npos++;

    examine_position(EXAMINE_ALL, 0);		/* GNU Go dragon/owl analysis */

    for (rr = 0; rr < 9; rr++)
      for (cc = 0; cc < 9; cc++) {
	int b = board[POS(rr, cc)];
	cells[rr * 9 + cc] = (b == BLACK) ? 1 : (b == WHITE) ? 2 : 0;
      }
    tugo_ownership_ld(cells, 9, budget, own);	/* tugo proven ownership */

    if (getenv("MCDIFF_DUMP") && npos == 1) {
      int rr2, cc2;
      fprintf(stderr, "board (B/W/.) | dragon_status | tugo_own(b/w/.)\n");
      for (rr2 = 0; rr2 < 9; rr2++) {
	for (cc2 = 0; cc2 < 9; cc2++) {
	  int bb = board[POS(rr2, cc2)];
	  fprintf(stderr, "%c", bb == BLACK ? 'B' : bb == WHITE ? 'W' : '.');
	}
	fprintf(stderr, "  ");
	for (cc2 = 0; cc2 < 9; cc2++) {
	  int bb = board[POS(rr2, cc2)];
	  enum dragon_status s = (bb == BLACK || bb == WHITE) ? dragon_status(POS(rr2, cc2)) : UNKNOWN;
	  fprintf(stderr, "%c", bb == EMPTY ? '.' : s == ALIVE ? 'a' : s == DEAD ? 'd' : '?');
	}
	fprintf(stderr, "  ");
	for (cc2 = 0; cc2 < 9; cc2++) {
	  int o = own[rr2 * 9 + cc2];
	  fprintf(stderr, "%c", o == 1 ? 'b' : o == 2 ? 'w' : '.');
	}
	fprintf(stderr, "\n");
      }
      return 0;
    }

    for (rr = 0; rr < 9; rr++)
      for (cc = 0; cc < 9; cc++) {
	int pos = POS(rr, cc);
	int b = board[pos];
	int t, t_owner, g_owner;
	enum dragon_status st;
	if (b != BLACK && b != WHITE)		/* stones only */
	  continue;
	t = own[rr * 9 + cc];			/* 1=black 2=white 0=abstain */
	if (t == 0)				/* tugo didn't prove this point */
	  continue;
	t_owner = (t == 1) ? BLACK : WHITE;
	st = dragon_status(pos);
	g_owner = (st == ALIVE) ? b : (st == DEAD) ? OTHER_COLOR(b) : EMPTY;
	if (g_owner == EMPTY)			/* GNU Go not definite (CRITICAL/UNKNOWN) */
	  continue;
	nproven++;
	if (t_owner != g_owner) {
	  ndisagree++;
	  printf("DISAGREE pos#%d %c%d: stone=%s  gnugo=%s(->%s)  tugo_proves=%s\n",
		 npos, 'A' + cc + (cc >= 8), rr + 1,
		 (b == BLACK) ? "B" : "W",
		 (st == ALIVE) ? "ALIVE" : "DEAD",
		 (g_owner == BLACK) ? "black" : "white",
		 (t_owner == BLACK) ? "black" : "white");
	}
      }
  }
  fclose(f);
  fprintf(stderr, "positions=%d  proven-stone-comparisons=%d  DISAGREEMENTS=%d\n",
	  npos, nproven, ndisagree);
  return 0;
}
