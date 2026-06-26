/* mctrain -- fit the Monte Carlo playout policy (Tier 1).
 *
 * Self-plays games with the *classical* GNU Go engine (a fast, much-stronger-
 * than-random teacher) from randomized openings, and at every move records
 * which value-table index the played move samples under together with the
 * indices of all legal alternatives.  It then fits a Bradley-Terry / Plackett-
 * Luce "choose-one" model by Minorization-Maximization (Coulom 2007, "Computing
 * Elo Ratings of Move Patterns in the Game of Go"): each index i gets a
 * strength gamma_i, and the probability the teacher picks move m among the
 * legal field F is gamma_{idx(m)} / sum_{k in F} gamma_{idx(k)}.  The fitted
 * gamma are exactly the per-move sampling weights the playout wants, so they
 * are scaled to uint and written as a raw values table loadable via
 * GNUGO_MC_VALUES.
 *
 * Everything (game generation + feature extraction + training) runs in this one
 * process against libengine.a -- no GTP round-trips, no external data.
 *
 * Usage: mctrain <ngames> <maxmoves> <boardsize> <out.values> [seed] [mm_iters]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "gnugo.h"
#include "liberty.h"
#include "patterns.h"		/* mc_pattern_databases (the baseline table) */
#include "random.h"

/* Growable store of training examples.  Each example is a winner index plus the
 * field of candidate indices (the winner is included in the field). */
static int *field;		/* concatenated candidate indices */
static long field_len, field_cap;
static long *ex_off;		/* offset into field[] of each example */
static int  *ex_len;		/* number of candidates in each example */
static int  *ex_win;		/* winning index of each example */
static long n_ex, ex_cap;

static void
push_example(int winner, const int *ids, int n)
{
  int k;
  if (n_ex >= ex_cap) {
    ex_cap = ex_cap ? ex_cap * 2 : 1 << 16;
    ex_off = realloc(ex_off, ex_cap * sizeof(*ex_off));
    ex_len = realloc(ex_len, ex_cap * sizeof(*ex_len));
    ex_win = realloc(ex_win, ex_cap * sizeof(*ex_win));
  }
  if (field_len + n > field_cap) {
    while (field_len + n > field_cap)
      field_cap = field_cap ? field_cap * 2 : 1 << 20;
    field = realloc(field, field_cap * sizeof(*field));
  }
  ex_off[n_ex] = field_len;
  ex_len[n_ex] = n;
  ex_win[n_ex] = winner;
  for (k = 0; k < n; k++)
    field[field_len++] = ids[k];
  n_ex++;
}

int
main(int argc, char **argv)
{
  int ngames, maxmoves, boardsize;
  const char *outfile;
  unsigned int seed = 1;
  int mm_iters = 40;
  int level = 4;		/* teacher strength: low is fast & still strong */
  int table_size;
  double *gamma, *W, *denom;
  static int moves[BOARDMAX], ids[BOARDMAX];
  int g, i, iter;
  double mean;
  long trained = 0;
  unsigned int *values;
  FILE *fp;

  if (argc < 5) {
    fprintf(stderr, "usage: %s <ngames> <maxmoves> <boardsize> <out.values>"
	    " [seed] [mm_iters]\n", argv[0]);
    return 1;
  }
  ngames = atoi(argv[1]);
  maxmoves = atoi(argv[2]);
  boardsize = atoi(argv[3]);
  outfile = argv[4];
  if (argc > 5)
    seed = (unsigned int) atoi(argv[5]);
  if (argc > 6)
    mm_iters = atoi(argv[6]);
  if (argc > 7)
    level = atoi(argv[7]);

  init_gnugo(20.0, seed);
  set_level(level);
  gnugo_clear_board(boardsize);
  table_size = mc_get_size_of_pattern_values_table();

  /* ---- Self-play + feature extraction. ---- */
  for (g = 0; g < ngames; g++) {
    int color = BLACK;
    int passes = 0, played = 0, resign;
    int nopen, r;

    gnugo_clear_board(boardsize);
    nopen = 2 + (int) (gg_drand() * 5.0);		/* random opening */
    for (r = 0; r < nopen; r++) {
      int n = mc_move_pattern_ids(color, moves, ids);
      if (n == 0)
	break;
      gnugo_play_move(moves[(int) (gg_drand() * n)], color);
      color = OTHER_COLOR(color);
    }

    while (passes < 2 && played < maxmoves) {
      int n = mc_move_pattern_ids(color, moves, ids);
      int move, k, chosen = -1;
      move = genmove(color, NULL, &resign);
      if (resign)
	break;
      if (move != PASS_MOVE && n > 1) {
	for (k = 0; k < n; k++)
	  if (moves[k] == move) {
	    chosen = ids[k];
	    break;
	  }
	if (chosen >= 0)
	  push_example(chosen, ids, n);
      }
      gnugo_play_move(move, color);
      passes = (move == PASS_MOVE) ? passes + 1 : 0;
      color = OTHER_COLOR(color);
      played++;
    }
    if ((g + 1) % 100 == 0)
      fprintf(stderr, "  %d/%d games, %ld examples\n", g + 1, ngames, n_ex);
  }
  fprintf(stderr, "collected %ld examples (%ld candidate entries)\n",
	  n_ex, field_len);

  /* ---- Minorization-Maximization fit of the Bradley-Terry strengths. ---- */
  gamma = malloc(table_size * sizeof(*gamma));
  W = calloc(table_size, sizeof(*W));
  denom = malloc(table_size * sizeof(*denom));
  for (i = 0; i < table_size; i++)
    gamma[i] = 1.0;
  for (g = 0; g < n_ex; g++)			/* win counts */
    W[ex_win[g]] += 1.0;

  for (iter = 0; iter < mm_iters; iter++) {
    double ll = 0.0;
    for (i = 0; i < table_size; i++)
      denom[i] = 0.0;
    for (g = 0; g < n_ex; g++) {
      long off = ex_off[g];
      int len = ex_len[g], k;
      double S = 0.0;
      for (k = 0; k < len; k++)
	S += gamma[field[off + k]];
      ll += log(gamma[ex_win[g]] / S);
      for (k = 0; k < len; k++)
	denom[field[off + k]] += 1.0 / S;
    }
    for (i = 0; i < table_size; i++)
      if (W[i] > 0.0)
	gamma[i] = W[i] / denom[i];
      else if (denom[i] > 0.0)
	gamma[i] = 0.0;			/* competed, never chosen */
      /* else: never seen -> keep prior gamma = 1 */
    fprintf(stderr, "  MM iter %2d  avg log-likelihood %.4f\n",
	    iter + 1, ll / (n_ex ? n_ex : 1));
  }

  /* ---- Blend onto the baseline table. ----
   * Only a few hundred shapes appear in real games; the rest of the 283k-entry
   * table must KEEP its tuned baseline value (replacing it with a uniform
   * default destroys the playout policy).  So start from the baseline and
   * overwrite only the trained contexts, scaling the learned strengths to the
   * baseline's magnitude over those same contexts. */
  {
    const unsigned int *base = mc_pattern_databases[0].values;
    double base_mean = 0.0, gamma_mean = 0.0, scale;
    long bmin = -1, bmax = 0;
    values = malloc(table_size * sizeof(*values));
    memcpy(values, base, table_size * sizeof(*values));
    for (i = 0; i < table_size; i++) {
      if (bmin < 0 || (long) base[i] < bmin)
	bmin = base[i];
      if ((long) base[i] > bmax)
	bmax = base[i];
      if (W[i] > 0.0 || denom[i] > 0.0) {
	base_mean += base[i];
	gamma_mean += gamma[i];
	trained++;
      }
    }
    fprintf(stderr, "baseline values: min %ld max %ld; trained contexts %ld\n",
	    bmin, bmax, trained);
    base_mean = trained ? base_mean / trained : 1.0;
    gamma_mean = trained ? gamma_mean / trained : 1.0;
    scale = (gamma_mean > 0.0) ? base_mean / gamma_mean : 1.0;
    mean = base_mean;
    for (i = 0; i < table_size; i++)
      if (W[i] > 0.0 || denom[i] > 0.0) {
	double v = gamma[i] * scale;		/* onto baseline scale */
	if (v < 1.0)
	  v = 1.0;
	if (v > 1000000.0)
	  v = 1000000.0;
	values[i] = (unsigned int) (v + 0.5);
      }
  }

  fp = fopen(outfile, "wb");
  if (!fp) {
    fprintf(stderr, "cannot open %s for writing\n", outfile);
    return 1;
  }
  if (fwrite(values, sizeof(*values), table_size, fp) != (size_t) table_size) {
    fprintf(stderr, "short write to %s\n", outfile);
    fclose(fp);
    return 1;
  }
  fclose(fp);
  fprintf(stderr, "wrote %d values to %s (%ld trained, mean gamma %.3f)\n",
	  table_size, outfile, trained, mean);
  return 0;
}
