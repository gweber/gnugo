/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *\
 * This is GNU Go, a Go program. Contact gnugo@gnu.org, or see       *
 * http://www.gnu.org/software/gnugo/ for more information.          *
 *                                                                   *
 * This program is free software; you can redistribute it and/or     *
 * modify it under the terms of the GNU General Public License as    *
 * published by the Free Software Foundation - version 3.            *
\* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef _DFPN_H_
#define _DFPN_H_

/* Depth-first proof-number (df-pn) search for the capturing game.
 *
 * This is a self-contained, modern best-first tactical solver built on top of
 * the existing board engine (trymove/popgo/board_hash). Where reading.c uses
 * a heuristic depth/node-bounded DFS, df-pn expands the node that is easiest
 * to prove or disprove next, guided by proof/disproof numbers -- the SOTA
 * approach for tsume-go / capturing-race solving (Nagai 2002; Kishimoto &
 * Mueller). See the references in dfpn.c.
 *
 * It is currently exposed only via the GTP test command `dfpn_attack` and is
 * NOT wired into genmove; it is a foundation for later replacing the
 * heuristic termination in reading.c / owl.c with proof-number search.
 *
 * This prototype is scoped to the LADDER game (see dfpn.c): it is sound but
 * deliberately incomplete -- it proves ladders and direct captures and
 * reports "safe" otherwise. Validated to agree with the engine's attack()
 * with zero unsound results.
 */

/* Result codes. */
#define DFPN_PROVED     1   /* attacker can capture the string */
#define DFPN_DISPROVED  0   /* string is safe against capture */
#define DFPN_UNKNOWN   -1   /* node limit hit before a conclusion */

/* Try to prove that the side NOT owning the string at `str` can capture it.
 * `node_limit` bounds the number of expanded nodes (0 -> a sensible default).
 * If `attack_move` is non-NULL it receives the proven first attacking move
 * (only meaningful when the result is DFPN_PROVED). Returns one of the
 * DFPN_* codes above. The board is left unchanged. */
int dfpn_capture(int str, int node_limit, int *attack_move);

#endif /* _DFPN_H_ */
