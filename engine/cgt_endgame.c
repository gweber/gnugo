/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *\
 * This is GNU Go, a Go program. Contact gnugo@gnu.org, or see       *
 * http://www.gnu.org/software/gnugo/ for more information.          *
 *                                                                   *
 * Copyright 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007,   *
 * 2008, 2009, 2010 and 2011 by the Free Software Foundation.        *
 *                                                                   *
 * This program is free software; you can redistribute it and/or     *
 * modify it under the terms of the GNU General Public License as    *
 * published by the Free Software Foundation - version 3 or          *
 * (at your option) any later version.                               *
 *                                                                   *
 * This program is distributed in the hope that it will be useful,   *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of    *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the     *
 * GNU General Public License in file COPYING for more details.      *
 *                                                                   *
 * You should have received a copy of the GNU General Public         *
 * License along with this program; if not, write to the Free        *
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,       *
 * Boston, MA 02111, USA.                                            *
\* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * CGT-lite: exact local-endgame temperatures for independent, capture-free
 * regions.  Ported from the tugo engine (rust/tugo-net/src/cgt.rs).
 *
 * The classical move generator picks endgame moves from pattern matches and
 * influence; it has no notion of combinatorial-game *temperature*, so in long
 * boundary corridors of similar value it can play a smaller point before a
 * bigger one.  This module computes, for each genuinely independent local
 * endgame, the exact left/right stops (and hence temperature = (L-R)/2) by
 * exhaustive minimax, and reports the hottest exact move available now.
 *
 * Soundness contract (abstain can't poison): a region is valued ONLY if
 *   1. it is a connected component of EMPTY points that are NOT unconditional
 *      territory of either color (i.e. genuinely open / contested), and
 *   2. every stone bordering it is unconditionally alive (a settled wall), so
 *      the local game is independent of the rest of the board, and
 *   3. it has at most CGT_MAX_REGION empty points (the minimax is exhaustive),
 *      and
 *   4. no capture occurs anywhere in its local game tree (a capture implies ko
 *      / non-monotone play this Stage-1 search does not model).
 * Any region violating any condition is silently dropped, never guessed.
 *
 * Capture-free + monotone (every non-pass move fills one empty) => the local
 * game is a finite acyclic DAG => memoised minimax is exact.  This is exactly
 * the dame / simple-gote boundary play that pattern-based endgame play orders
 * poorly.
 *
 * The whole module is inert unless the caller invokes cgt_endgame_move(); the
 * genmove hook that calls it is itself gated behind the GNUGO_CGT environment
 * variable, so default play is byte-identical to upstream.
 */

#include "gnugo.h"

#include <stdlib.h>
#include <string.h>

#include "liberty.h"

/* Max empty points in a region we will value (minimax is exhaustive over it). */
#define CGT_MAX_REGION 8
/* Per-region node budget; on overflow the region abstains. */
#define CGT_NODE_BUDGET 4000000L

/* Memo: keyed by (region signature, side-to-move bit, pass count in {0,1}).
 * The region signature is a base-3 number over the <= CGT_MAX_REGION region
 * cells (0 empty / 1 mover-color / 2 opponent), so it is < 3^CGT_MAX_REGION.
 * The rest of the board is a fixed unconditionally-alive constant.
 */
#define CGT_SIG_MAX 6561      /* 3^8 */
#define CGT_MEMO_SIZE (CGT_SIG_MAX * 4)

static float cgt_memo_val[CGT_MEMO_SIZE];
static char  cgt_memo_set[CGT_MEMO_SIZE];

/* Per-region working state (one region valued at a time; genmove is single
 * threaded, so file-static scratch is fine and avoids reallocation).
 */
static int cgt_pa_owner[BOARDMAX];   /* BLACK / WHITE / EMPTY per point        */
static int cgt_assigned[BOARDMAX];   /* point already placed in some component */
static int cgt_in_region[BOARDMAX];  /* point belongs to the current region    */
static int cgt_pts[CGT_MAX_REGION];  /* the current region's empty points      */
static int cgt_npts;
static int cgt_color;                /* the advised color = the maximizer      */
static long cgt_budget;

/* Base-3 signature of the current region cells, mover-relative. */
static int
cgt_region_sig(void)
{
  int sig = 0;
  int mult = 1;
  int i;
  for (i = 0; i < cgt_npts; i++) {
    int c = board[cgt_pts[i]];
    int d = (c == EMPTY) ? 0 : (c == cgt_color) ? 1 : 2;
    sig += d * mult;
    mult *= 3;
  }
  return sig;
}

/* Tromp-Taylor area score of the region, from cgt_color's perspective
 * (own area minus opponent area).  Only the region's own cells vary between
 * terminal positions; the rest of the board is a fixed constant, so this net
 * is all the minimax needs to compare.  An empty region cell is owned by a
 * color iff every cell reachable from it through empties touches only that
 * color (its walls are unconditionally-alive stones / territory).
 */
static float
cgt_region_net(void)
{
  static int flood_gen = 0;
  static int flood_mark[BOARDMAX];
  float net = 0.0;
  int i;

  flood_gen++;
  for (i = 0; i < cgt_npts; i++) {
    int p = cgt_pts[i];
    if (board[p] == cgt_color)
      net += 1.0;
    else if (board[p] != EMPTY)
      net -= 1.0;
    else if (flood_mark[p] != flood_gen) {
      /* Flood the maximal empty-connected blob of region cells; collect the
       * colors of the stones / territory that border it.
       */
      int stack[CGT_MAX_REGION];
      int sp = 0;
      int count = 0;
      int touch_black = 0;
      int touch_white = 0;
      stack[sp++] = p;
      flood_mark[p] = flood_gen;
      while (sp > 0) {
	int q = stack[--sp];
	int k;
	count++;
	for (k = 0; k < 4; k++) {
	  int nb = q + delta[k];
	  int owner;
	  if (!ON_BOARD(nb))
	    continue;
	  if (board[nb] == EMPTY) {
	    if (cgt_in_region[nb]) {
	      if (flood_mark[nb] != flood_gen) {
		flood_mark[nb] = flood_gen;
		stack[sp++] = nb;
	      }
	      continue;
	    }
	    /* Empty but outside the region: settled territory; use its owner. */
	    owner = cgt_pa_owner[nb];
	  }
	  else
	    owner = board[nb];
	  if (owner == BLACK)
	    touch_black = 1;
	  else if (owner == WHITE)
	    touch_white = 1;
	}
      }
      if (touch_black && !touch_white)
	net += (cgt_color == BLACK) ? count : -count;
      else if (touch_white && !touch_black)
	net += (cgt_color == WHITE) ? count : -count;
      /* else neutral (dame): contributes 0 */
    }
  }
  return net;
}

/* Minimax the local game.  cgt_color maximizes its own-perspective net area;
 * the opponent minimizes.  Terminal = two consecutive local passes.  Returns 1
 * with the value in *out, or 0 to ABSTAIN the whole region (a capture occurred
 * or the node budget was exhausted); 0 propagates up so the caller drops it.
 */
static int
cgt_solve(int to_move, int passes, float *out)
{
  int idx;
  int maximize;
  int have_best = 0;
  float best = 0.0;
  float v;
  int i;

  if (passes >= 2) {
    *out = cgt_region_net();
    return 1;
  }
  if (cgt_budget <= 0)
    return 0;
  cgt_budget--;

  idx = cgt_region_sig() * 4 + ((to_move == cgt_color) ? 0 : 2) + passes;
  if (cgt_memo_set[idx]) {
    *out = cgt_memo_val[idx];
    return 1;
  }

  maximize = (to_move == cgt_color);

  /* Moves: any empty region point that is legal and captures nothing. */
  for (i = 0; i < cgt_npts; i++) {
    int p = cgt_pts[i];
    if (board[p] != EMPTY)
      continue;
    if (does_capture_something(p, to_move))
      return 0;                 /* capture => ko risk => abstain region */
    if (!trymove(p, to_move, "cgt_endgame", NO_MOVE))
      continue;                 /* illegal (suicide etc.) => skip */
    if (!cgt_solve(OTHER_COLOR(to_move), 0, &v)) {
      popgo();
      return 0;                 /* abstain propagates up */
    }
    popgo();
    if (!have_best || (maximize ? v > best : v < best)) {
      best = v;
      have_best = 1;
    }
  }

  /* Pass is always available; it (eventually) ends the local game. */
  if (!cgt_solve(OTHER_COLOR(to_move), passes + 1, &v))
    return 0;
  if (!have_best || (maximize ? v > best : v < best))
    best = v;

  cgt_memo_val[idx] = best;
  cgt_memo_set[idx] = 1;
  *out = best;
  return 1;
}

/* The best first move for cgt_color in the current region, IFF playing strictly
 * beats passing there (the region is "hot" for it).  Returns NO_MOVE if passing
 * is at least as good, or the region abstains.  Reuses the (already populated)
 * memo of cgt_solve.
 */
static int
cgt_find_best_move(void)
{
  int opp = OTHER_COLOR(cgt_color);
  float pass_val;
  float best = 0.0;
  int best_mv = NO_MOVE;
  int have = 0;
  float v;
  int i;

  /* Value of cgt_color NOT playing here (it passes; the local game continues). */
  if (!cgt_solve(opp, 1, &pass_val))
    return NO_MOVE;

  for (i = 0; i < cgt_npts; i++) {
    int p = cgt_pts[i];
    if (board[p] != EMPTY)
      continue;
    if (does_capture_something(p, cgt_color))
      return NO_MOVE;
    if (!trymove(p, cgt_color, "cgt_endgame", NO_MOVE))
      continue;
    if (!cgt_solve(opp, 0, &v)) {
      popgo();
      return NO_MOVE;
    }
    popgo();
    if (!have || v > best) {
      best = v;
      best_mv = p;
      have = 1;
    }
  }

  if (have && best > pass_val + 1e-4)
    return best_mv;
  return NO_MOVE;            /* playing here gains nothing over passing */
}

/* Flood the connected component of open empty points starting at `start`,
 * recording it in cgt_pts / cgt_in_region.  Returns the component size, or -1
 * if it exceeds CGT_MAX_REGION (caller abstains).  Marks every visited point in
 * cgt_assigned regardless, so an oversized component is not revisited.
 */
static int
cgt_flood_component(int start)
{
  int stack[BOARDMAX];
  int sp = 0;
  int n = 0;
  int oversized = 0;

  stack[sp++] = start;
  cgt_assigned[start] = 1;
  while (sp > 0) {
    int p = stack[--sp];
    int k;
    if (n < CGT_MAX_REGION) {
      cgt_pts[n] = p;
      cgt_in_region[p] = 1;
    }
    else
      oversized = 1;
    n++;
    for (k = 0; k < 4; k++) {
      int nb = p + delta[k];
      if (ON_BOARD(nb)
	  && !cgt_assigned[nb]
	  && board[nb] == EMPTY
	  && cgt_pa_owner[nb] == EMPTY) {
	cgt_assigned[nb] = 1;
	stack[sp++] = nb;
      }
    }
  }

  if (oversized || n > CGT_MAX_REGION) {
    int i;
    for (i = 0; i < n && i < CGT_MAX_REGION; i++)
      cgt_in_region[cgt_pts[i]] = 0;
    return -1;
  }
  cgt_npts = n;
  return n;
}

/* True if the current region (cgt_pts/cgt_in_region) is enclosed entirely by
 * unconditionally-alive walls: every on-board stone bordering it is pass-alive.
 */
static int
cgt_region_enclosed(void)
{
  int i;
  for (i = 0; i < cgt_npts; i++) {
    int k;
    for (k = 0; k < 4; k++) {
      int nb = cgt_pts[i] + delta[k];
      if (!ON_BOARD(nb))
	continue;
      if (board[nb] != EMPTY && cgt_pa_owner[nb] == EMPTY)
	return 0;             /* a non-pass-alive stone touches the region */
    }
  }
  return 1;
}

/* Compute the unconditional-territory owner of every point. */
static void
cgt_compute_pa_owner(void)
{
  static int ut_black[BOARDMAX];
  static int ut_white[BOARDMAX];
  int pos;

  unconditional_life(ut_black, BLACK);
  unconditional_life(ut_white, WHITE);
  for (pos = BOARDMIN; pos < BOARDMAX; pos++) {
    if (!ON_BOARD(pos))
      cgt_pa_owner[pos] = EMPTY;
    else if (ut_black[pos])
      cgt_pa_owner[pos] = BLACK;
    else if (ut_white[pos])
      cgt_pa_owner[pos] = WHITE;
    else
      cgt_pa_owner[pos] = EMPTY;
  }
}

/*
 * Find the exact CGT-hottest endgame move for `color`.  Returns the move point,
 * or NO_MOVE if there is no sound hot region (everything settled or abstained).
 * On a hit, *temperature receives the region's temperature (in points); it is
 * left at 0.0 otherwise.  Sound by construction (see file header): only
 * capture-free regions enclosed by unconditionally-alive walls, of size at most
 * CGT_MAX_REGION, are considered.
 *
 * The board is left exactly as it was found (all probing moves are undone).
 */
int
cgt_endgame_move(int color, float *temperature)
{
  int start;
  int best_move = NO_MOVE;
  float best_temp = 0.0;

  if (temperature)
    *temperature = 0.0;

  cgt_color = color;
  cgt_compute_pa_owner();
  memset(cgt_assigned, 0, sizeof(cgt_assigned));
  memset(cgt_in_region, 0, sizeof(cgt_in_region));

  for (start = BOARDMIN; start < BOARDMAX; start++) {
    float left, right, temp;
    int mv;

    if (!ON_BOARD(start)
	|| cgt_assigned[start]
	|| board[start] != EMPTY
	|| cgt_pa_owner[start] != EMPTY)
      continue;

    if (cgt_flood_component(start) < 0)
      continue;                 /* oversized => abstain */

    if (cgt_region_enclosed()) {
      /* Exact minimax for both first-movers; abstain on capture/budget. */
      cgt_budget = CGT_NODE_BUDGET;
      memset(cgt_memo_set, 0, sizeof(cgt_memo_set));
      if (cgt_solve(color, 0, &left)
	  && cgt_solve(OTHER_COLOR(color), 0, &right)) {
	temp = (left - right) * 0.5;
	if (temp > best_temp + 1e-6) {
	  mv = cgt_find_best_move();
	  if (mv != NO_MOVE) {
	    best_temp = temp;
	    best_move = mv;
	  }
	}
      }
    }

    /* Clear this region's membership flags before moving on. */
    {
      int i;
      for (i = 0; i < cgt_npts; i++)
	cgt_in_region[cgt_pts[i]] = 0;
    }
  }

  ASSERT1(stackp == 0, best_move);
  if (best_move != NO_MOVE && temperature)
    *temperature = best_temp;
  return best_move;
}
