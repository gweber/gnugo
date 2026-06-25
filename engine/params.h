/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *\
 * This is GNU Go, a Go program. Contact gnugo@gnu.org, or see       *
 * http://www.gnu.org/software/gnugo/ for more information.          *
 *                                                                   *
 * This program is free software; you can redistribute it and/or     *
 * modify it under the terms of the GNU General Public License as    *
 * published by the Free Software Foundation - version 3.            *
\* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef _PARAMS_H_
#define _PARAMS_H_

/* Runtime-tunable strength parameters.
 *
 * Every field defaults to the historical hard-coded value, so the engine
 * plays identically unless a value is overridden via an environment variable
 * (see load_tunable_params() in interface.c). This exposes the most
 * strength-critical scalar knobs to automated tuning (CLOP / SPSA) driven by
 * the self-play harness in regression/selfplay/ -- without forking behavior
 * for ordinary use.
 *
 * The *_weight_scale fields multiply the move-valuation weights that
 * choose_strategy() computes, so a scale of 1.0 leaves behavior unchanged in
 * both the default and cosmic modes.
 */
struct tunable_params {
  float territorial_weight_scale;    /* scales territorial_weight */
  float strategical_weight_scale;    /* scales strategical_weight */
  float attack_dragon_weight_scale;  /* scales attack_dragon_weight */
  float followup_weight_scale;       /* scales followup_weight */
  float invasion_malus_weight_scale; /* scales invasion_malus_weight */
  float shape_factor_base;           /* base of the shape factor, was 1.05 */
  float lunch_weakness_multiplier;   /* lunch attack value mult, was 1.8 */
};

extern struct tunable_params tunable;

/* Populate `tunable` from GNUGO_* environment variables. Called once from
 * init_gnugo(). Safe to call more than once. */
void load_tunable_params(void);

#endif /* _PARAMS_H_ */
