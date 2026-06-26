/* mccalib -- measure how well GNU Go's Monte Carlo playouts estimate position
 * value, against KataGo-labeled targets (simulation-balancing diagnostic).
 *
 * Reads positions decoded by decode_positions.py, sets each up on the board,
 * runs M playouts under the current playout policy (baseline, or a learned one
 * via GNUGO_MC_VALUES), and prints "<target> <playout_winrate>" per line.  A
 * well-balanced playout has playout_winrate ~ target; a large systematic gap is
 * room for simulation balancing.
 *
 * Usage: mccalib <positions.txt> <playouts_per_pos>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gnugo.h"
#include "liberty.h"
#include "patterns.h"

int
main(int argc, char **argv)
{
  FILE *f;
  int M;
  char ctm[4], col[4];
  double target;
  int nstones, r, c, i;

  if (argc < 3) {
    fprintf(stderr, "usage: %s <positions.txt> <playouts_per_pos>\n", argv[0]);
    return 1;
  }
  M = atoi(argv[2]);

  init_gnugo(20.0, 1);
  gnugo_clear_board(9);
  choose_mc_patterns(NULL);	/* loads default (or GNUGO_MC_VALUES) playout table */
  komi = 7.5;

  f = fopen(argv[1], "r");
  if (!f) {
    fprintf(stderr, "cannot open %s\n", argv[1]);
    return 1;
  }

  {
  int use_estimate = (argc > 3 && strcmp(argv[3], "estimate") == 0);
  while (fscanf(f, "%lf %3s %d", &target, ctm, &nstones) == 3) {
    int ctm_color = (ctm[0] == 'B') ? BLACK : WHITE;
    float vth;
    gnugo_clear_board(9);
    for (i = 0; i < nstones; i++) {
      if (fscanf(f, "%d %d %3s", &r, &c, col) != 3)
	break;
      add_stone(POS(r, c), (col[0] == 'B') ? BLACK : WHITE);
    }
    if (use_estimate) {
      /* GNU Go's own dragon/L&D-aware score estimate. score>=0 => White ahead. */
      float ub, lb;
      float score = gnugo_estimate_score(&ub, &lb);
      int ctm_wins = (ctm_color == WHITE) ? (score >= 0.0) : (score < 0.0);
      vth = ctm_wins ? 1.0 : 0.0;
    }
    else
      vth = mc_playout_value(ctm_color, M);
    printf("%.4f %.4f\n", target, vth);
  }
  }
  fclose(f);
  return 0;
}
