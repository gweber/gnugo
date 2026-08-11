/* Differential test: GNU Go's heuristic dragon/owl life-and-death vs saigo's
 * SOUND df-pn oracle (formerly tugo).  For each decoded position, compare GNU
 * Go's dragon_status of every stone against saigo's PROVEN ownership of that
 * point.  saigo only returns a verdict it has proven (Benson pass-alive +
 * df-pn) and abstains otherwise, so any stone where saigo proves an owner that
 * contradicts GNU Go's OWL-READ ALIVE/DEAD is a GNU Go reading bug -- saigo
 * can't be the wrong one.
 *
 * Only owl-analyzed dragons are compared: make_dragons deliberately skips owl
 * reading for dragons whose crude weakness looks negligible (dragon.c
 * "Some dragons can be ignored") and defaults their status to ALIVE, so that
 * ALIVE is a heuristic guess, not a reading verdict.  Those stones are counted
 * as owl-unchecked instead of compared (their tactics live at the worm level,
 * where e.g. a lone stone in atari is correctly attackable-with-no-defense).
 *
 * Side to move is deliberately ignored: both engines make tempo-independent
 * claims.  GNU Go's ALIVE/DEAD are worst-case verdicts (no attack even moving
 * first / no defense even moving first) and CRITICAL is skipped below; saigo
 * abstains on anything tempo- or ko-dependent.
 *
 * Usage: mcdiff <positions.txt> [ld_budget]   (decode_positions.py format)
 * Env:   MCDIFF_DUMP=1  also renders position 1's three grids to stderr.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gnugo.h"
#include "liberty.h"
#include "patterns.h"
#include "mcpos.h"

int
main(int argc, char **argv)
{
  FILE *f;
  char ctm[4];
  double target;
  unsigned long budget = (argc > 2) ? strtoul(argv[2], NULL, 10) : 100000;
  int ndisagree = 0, nproven = 0, npos = 0, nunchecked = 0, nconceded = 0;
  int dump;
  unsigned char cells[81], own[81];

  if (argc < 2) {
    fprintf(stderr, "usage: %s <positions.txt> [ld_budget]\n", argv[0]);
    return 1;
  }
  init_gnugo(20.0, 1);
  gnugo_clear_board(9);
  komi = 7.5;
  dump = getenv("MCDIFF_DUMP") != NULL;

  f = fopen(argv[1], "r");
  if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }

  while (mc_read_position(f, &target, ctm, npos + 1)) {
    int rr, cc;
    npos++;

    /* reset_engine() is what installs the reading/owl depth limits
     * (set_depth_values); without it they are all zero, every owl read
     * aborts at its first node and every stone comes back ALIVE.
     * EXAMINE_DRAGONS is the stage that finalizes dragon[].status; the
     * further EXAMINE_ALL stages only add influence/weakness data. */
    reset_engine();
    examine_position(EXAMINE_DRAGONS, 0);

    mc_board_to_cells(cells);
    saigo_ownership_ld(cells, 9, budget, own);	/* saigo proven ownership */

    if (dump && npos == 1) {
      /* Top-to-bottom in the SOURCE-game frame (row 9 first), matching the
       * row labels printed by the DISAGREE lines below. */
      int rr2, cc2;
      fprintf(stderr, "row  board (B/W/.) | dragon_status | saigo_own(b/w/.)\n");
      for (rr2 = 8; rr2 >= 0; rr2--) {
	fprintf(stderr, "%d    ", rr2 + 1);
	for (cc2 = 0; cc2 < 9; cc2++) {
	  int bb = board[POS(rr2, cc2)];
	  fprintf(stderr, "%c", bb == BLACK ? 'B' : bb == WHITE ? 'W' : '.');
	}
	fprintf(stderr, "      ");
	for (cc2 = 0; cc2 < 9; cc2++) {
	  int bb = board[POS(rr2, cc2)];
	  enum dragon_status s = (bb == BLACK || bb == WHITE) ? dragon_status(POS(rr2, cc2)) : UNKNOWN;
	  fprintf(stderr, "%c", bb == EMPTY ? '.' : s == ALIVE ? 'a' : s == DEAD ? 'd' : '?');
	}
	fprintf(stderr, "         ");
	for (cc2 = 0; cc2 < 9; cc2++) {
	  int o = own[rr2 * 9 + cc2];
	  fprintf(stderr, "%c", o == 1 ? 'b' : o == 2 ? 'w' : '.');
	}
	fprintf(stderr, "\n");
      }
      /* fall through: the dumped position is also compared and counted */
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
	if (t == 0)				/* saigo didn't prove this point */
	  continue;
	if (DRAGON2(pos).owl_status == UNCHECKED) {
	  nunchecked++;				/* owl never read this dragon;
						 * its ALIVE is only a default */
	  continue;
	}
	/* dragon_status is dragon-granular: a tactically lost stone (attack
	 * with no defense) inside an otherwise alive dragon is a conceded
	 * sacrifice/lunch, not a life claim about this point -- gnugo and the
	 * oracle already agree the stone falls, so there is nothing to test. */
	if (worm[pos].attack_codes[0] != 0 && worm[pos].defense_codes[0] == 0) {
	  nconceded++;
	  continue;
	}
	t_owner = (t == 1) ? BLACK : WHITE;
	st = dragon_status(pos);
	g_owner = (st == ALIVE) ? b : (st == DEAD) ? OTHER_COLOR(b) : EMPTY;
	if (g_owner == EMPTY)			/* GNU Go not definite (CRITICAL/UNKNOWN) */
	  continue;
	nproven++;
	if (t_owner != g_owner) {
	  ndisagree++;
	  /* rr + 1 is the SOURCE-game GTP row, not gnugo's internal row
	   * (the input frame is bottom-up; see mcpos.h). */
	  printf("DISAGREE pos#%d %c%d: stone=%s  gnugo=%s(->%s)  saigo_proves=%s\n",
		 npos, 'A' + cc + (cc >= 8), rr + 1,
		 (b == BLACK) ? "B" : "W",
		 (st == ALIVE) ? "ALIVE" : "DEAD",
		 (g_owner == BLACK) ? "black" : "white",
		 (t_owner == BLACK) ? "black" : "white");
	}
      }
  }
  fclose(f);
  fprintf(stderr, "positions=%d  proven-stone-comparisons=%d  owl-unchecked-skips=%d  conceded-stone-skips=%d  DISAGREEMENTS=%d\n",
	  npos, nproven, nunchecked, nconceded, ndisagree);
  return 0;
}
