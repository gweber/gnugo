/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *\
 * This is GNU Go, a Go program. Contact gnugo@gnu.org, or see       *
 * http://www.gnu.org/software/gnugo/ for more information.          *
 *                                                                   *
 * This program is free software; you can redistribute it and/or     *
 * modify it under the terms of the GNU General Public License as    *
 * published by the Free Software Foundation - version 3.            *
\* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * Depth-first proof-number (df-pn) search for the capturing game.
 *
 * Where reading.c uses a heuristic, depth/node-bounded depth-first search,
 * df-pn is a best-first AND/OR search that always expands the node which is
 * easiest to prove or disprove next, using proof numbers (phi) and disproof
 * numbers (delta). It is the state-of-the-art family of algorithms for
 * tsume-go and capturing-race solving:
 *
 *   A. Nagai, "Df-pn Algorithm for Searching AND/OR Trees and Its
 *      Applications", PhD thesis, Univ. of Tokyo, 2002.
 *   A. Kishimoto & M. Mueller, "Search versus Knowledge for Solving Life
 *      and Death Problems in Go", AAAI 2005.
 *
 * We use the negamax formulation: at every node, phi is the proof number
 * and delta the disproof number of the proposition "the attacker (the side
 * NOT owning the target string) captures the target". For a node with the
 * player to move and children c:
 *
 *     phi(n)   = min_c  delta(c)
 *     delta(n) = sum_c  phi(c)
 *
 * which captures the OR/AND alternation automatically. A node is proved
 * when phi == 0 (delta == INF) and disproved when delta == 0 (phi == INF).
 *
 * This module is built on the existing board engine (trymove/popgo,
 * board_hash) and is intentionally self-contained. It is exposed only via
 * the GTP test command `dfpn_attack`; it is NOT wired into genmove. It is a
 * foundation for later replacing the heuristic termination in reading.c /
 * owl.c with proof-number search.
 *
 * Scope of this prototype: the LADDER game. The attacker plays only atari /
 * capture moves (a string with >= DFPN_LIB_LIMIT liberties has escaped); the
 * defender, always in atari, extends along its single liberty or captures an
 * adjacent attacker string that is in atari. Restricting to ataris is what
 * makes the solver SOUND -- it never claims a capture that depends on the
 * defender being unable to jump, make eyes or tenuki, since a laddered string
 * has none of those options. It is therefore sound but deliberately
 * incomplete: it proves ladders and direct captures, and reports "safe" for
 * non-ladder captures that need the richer move generation in reading.c.
 * Validated against the engine's own attack(): 0 unsound results over a
 * randomized battery (see regression/selfplay/validate_dfpn.py).
 */

#include "gnugo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "liberty.h"
#include "dfpn.h"

#define DFPN_INF        1000000000
#define DFPN_LIB_LIMIT  3      /* >= this many liberties escapes the ladder */
#define DFPN_MAX_DEPTH  80     /* board-stack depth guard */
#define DFPN_MAXMOVES   (MAXLIBS + MAXCHAIN)
#define DFPN_TT_SIZE    (1 << 16)
#define DFPN_DEFAULT_NODES 200000

/* Transposition table (always-replace, single probe, key-verified). */
struct dfpn_tt_entry {
  unsigned long key;
  int phi;
  int delta;
  int valid;
};
static struct dfpn_tt_entry dfpn_tt[DFPN_TT_SIZE];

static int dfpn_node_count;
static int dfpn_node_limit;

/* A position key that also distinguishes whose move it is. board_hash
 * already encodes the stones and ko; fold in the side to move. */
static unsigned long
dfpn_key(int mover)
{
  unsigned long k = (unsigned long) board_hash.hashval[0];
  if (mover == WHITE)
    k ^= 0x9e3779b97f4a7c15UL;
  return k;
}

static int
tt_lookup(unsigned long key, int *phi, int *delta)
{
  struct dfpn_tt_entry *e = &dfpn_tt[key & (DFPN_TT_SIZE - 1)];
  if (e->valid && e->key == key) {
    *phi = e->phi;
    *delta = e->delta;
    return 1;
  }
  return 0;
}

static void
tt_store(unsigned long key, int phi, int delta)
{
  struct dfpn_tt_entry *e = &dfpn_tt[key & (DFPN_TT_SIZE - 1)];
  e->key = key;
  e->phi = phi;
  e->delta = delta;
  e->valid = 1;
}

/* Terminal evaluation of the current position. In the negamax formulation
 * (phi, delta) are relative to the player to move (`mover`): phi is the proof
 * number that the player to move achieves its goal, delta the disproof
 * number. The attacker's goal is to capture the target; the defender's goal
 * is to keep it safe. A won leaf is (phi=0, delta=INF), a lost leaf is
 * (phi=INF, delta=0). Returns 1 and sets (phi, delta) if terminal. */
static int
dfpn_terminal(int str, int attacker, int mover, int *phi, int *delta)
{
  int owner = OTHER_COLOR(attacker);

  if (board[str] != owner) {
    /* The target has been captured: the attacker achieved its goal. */
    if (mover == attacker) {
      *phi = 0;            /* the mover (attacker) has won */
      *delta = DFPN_INF;
    }
    else {
      *phi = DFPN_INF;     /* the mover (defender) has lost */
      *delta = 0;
    }
    return 1;
  }
  if (countlib(str) >= DFPN_LIB_LIMIT) {
    /* The target is safe against this local search: defender's goal met. */
    if (mover != attacker) {
      *phi = 0;            /* the mover (defender) has won */
      *delta = DFPN_INF;
    }
    else {
      *phi = DFPN_INF;     /* the mover (attacker) has lost */
      *delta = 0;
    }
    return 1;
  }
  return 0;
}

/* Add `move` to moves[] if not already present. */
static int
add_move(int *moves, int n, int move)
{
  int i;
  for (i = 0; i < n; i++)
    if (moves[i] == move)
      return n;
  if (n < DFPN_MAXMOVES)
    moves[n++] = move;
  return n;
}

/* Generate candidate moves for `mover`, restricted to the LADDER game --
 * the subset in which the simple "play on the string's liberties" move model
 * is sound, because the defender is always in atari and has no other escape:
 *
 *   - The attacker only plays moves that keep the target in atari: if the
 *     target has 1 liberty it captures by filling it; if it has 2 liberties
 *     it plays either one to atari. With >= DFPN_LIB_LIMIT liberties the
 *     attacker has no ladder move and the defender has escaped.
 *   - The defender (necessarily in atari) extends along its single liberty,
 *     or captures an adjacent attacker string that is itself in atari (the
 *     classic ladder-breaking counter-capture).
 *
 * Restricting to ataris is what keeps the solver SOUND: it never claims a
 * capture that depends on the defender being unable to jump, make eyes or
 * tenuki, since in a ladder none of those are available. Returns the count. */
static int
dfpn_gen_moves(int str, int attacker, int mover, int *moves)
{
  int libs[MAXLIBS];
  int nlibs = findlib(str, MAXLIBS, libs);
  int n = 0;
  int i;

  if (mover == attacker) {
    /* Only atari/capture moves: at most 2 liberties may be played. */
    if (nlibs <= 2)
      for (i = 0; i < nlibs; i++)
	n = add_move(moves, n, libs[i]);
    return n;
  }

  /* Defender: extend along the (single) liberty ... */
  for (i = 0; i < nlibs; i++)
    n = add_move(moves, n, libs[i]);

  /* ... or capture an adjacent attacker string that is in atari. */
  {
    int adj[MAXCHAIN];
    int nadj = chainlinks(str, adj);
    for (i = 0; i < nadj; i++) {
      if (countlib(adj[i]) == 1) {
	int lib;
	findlib(adj[i], 1, &lib);
	n = add_move(moves, n, lib);
      }
    }
  }
  return n;
}

/* Compute (phi, delta) of the position reached after a move has been played,
 * with `child_mover` to move. Reads the transposition table; unseen interior
 * nodes are initialized to (1, 1) as usual for df-pn. */
static void
dfpn_child_numbers(int str, int attacker, int child_mover, int *phi, int *delta)
{
  if (dfpn_terminal(str, attacker, child_mover, phi, delta))
    return;
  if (!tt_lookup(dfpn_key(child_mover), phi, delta)) {
    *phi = 1;
    *delta = 1;
  }
}

static int
cap_add(int a, int b)
{
  long s = (long) a + (long) b;
  return s >= DFPN_INF ? DFPN_INF : (int) s;
}

/* The df-pn MID procedure: refine the proof/disproof numbers of the current
 * (non-terminal) position with the player `mover` to move, until they reach
 * the given thresholds. Results are stored in the transposition table. */
static void
dfpn_mid(int str, int attacker, int mover, int phi_t, int delta_t)
{
  int moves[DFPN_MAXMOVES];
  int nmoves = dfpn_gen_moves(str, attacker, mover, moves);
  int other = OTHER_COLOR(mover);

  dfpn_node_count++;

  for (;;) {
    int i;
    int phi_n, delta_n;
    int min_delta = DFPN_INF, min_delta2 = DFPN_INF;
    int sum_phi = 0;
    int best = -1, best_phi = 0;
    int legal = 0;

    /* Recompute children's numbers from the transposition table. */
    for (i = 0; i < nmoves; i++) {
      int cphi, cdelta;
      if (!trymove(moves[i], mover, "dfpn", str))
	continue;
      dfpn_child_numbers(str, attacker, other, &cphi, &cdelta);
      popgo();
      legal++;
      sum_phi = cap_add(sum_phi, cphi);
      if (cdelta < min_delta) {
	min_delta2 = min_delta;
	min_delta = cdelta;
	best = moves[i];
	best_phi = cphi;
      }
      else if (cdelta < min_delta2) {
	min_delta2 = cdelta;
      }
    }

    if (legal == 0) {
      /* The player to move has no capturing-game move: it loses. */
      phi_n = DFPN_INF;
      delta_n = 0;
    }
    else {
      phi_n = min_delta;
      delta_n = sum_phi;
    }

    tt_store(dfpn_key(mover), phi_n, delta_n);

    if (phi_n >= phi_t || delta_n >= delta_t
	|| dfpn_node_count >= dfpn_node_limit
	|| stackp >= DFPN_MAX_DEPTH)
      return;

    /* Recurse into the most-proving child with df-pn thresholds. */
    {
      int phi_t_child = cap_add(delta_t - delta_n, best_phi);
      int delta_t_child = (min_delta2 >= DFPN_INF) ? phi_t
			  : (phi_t < min_delta2 + 1 ? phi_t : min_delta2 + 1);
      if (trymove(best, mover, "dfpn", str)) {
	dfpn_mid(str, attacker, other, phi_t_child, delta_t_child);
	popgo();
      }
    }
  }
}

int
dfpn_capture(int str, int node_limit, int *attack_move)
{
  int attacker, phi, delta;

  if (attack_move)
    *attack_move = NO_MOVE;

  if (!ON_BOARD(str) || board[str] == EMPTY)
    return DFPN_DISPROVED;

  attacker = OTHER_COLOR(board[str]);
  dfpn_node_limit = node_limit > 0 ? node_limit : DFPN_DEFAULT_NODES;
  dfpn_node_count = 0;
  memset(dfpn_tt, 0, sizeof(dfpn_tt));

  /* The root is an attacker-to-move (OR) node. */
  if (!dfpn_terminal(str, attacker, attacker, &phi, &delta))
    dfpn_mid(str, attacker, attacker, DFPN_INF, DFPN_INF);

  if (!tt_lookup(dfpn_key(attacker), &phi, &delta)) {
    /* Root was terminal; recompute. */
    dfpn_terminal(str, attacker, attacker, &phi, &delta);
  }

  if (getenv("GNUGO_DFPN_DEBUG"))
    fprintf(stderr, "dfpn: str=%d attacker=%d phi=%d delta=%d nodes=%d\n",
	    str, attacker, phi, delta, dfpn_node_count);

  if (phi == 0) {
    /* Proved: find the attacking move whose resulting position is disproved
     * for the defender (delta == 0 there). */
    if (attack_move) {
      int moves[DFPN_MAXMOVES];
      int nmoves = dfpn_gen_moves(str, attacker, attacker, moves);
      int i;
      for (i = 0; i < nmoves; i++) {
	int cphi, cdelta;
	if (!trymove(moves[i], attacker, "dfpn", str))
	  continue;
	dfpn_child_numbers(str, attacker, OTHER_COLOR(attacker), &cphi, &cdelta);
	popgo();
	if (cdelta == 0) {
	  *attack_move = moves[i];
	  break;
	}
      }
    }
    return DFPN_PROVED;
  }
  if (delta == 0)
    return DFPN_DISPROVED;

  return DFPN_UNKNOWN;
}
