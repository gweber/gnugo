/* How often does GNU Go's move match KataGo's best move? */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gnugo.h"
#include "liberty.h"
#include "patterns.h"
int main(int argc, char **argv) {
  FILE *f; int kr,kc,nstones,r,c,i; char ctm[4],col[4];
  int match=0,total=0,kpass_n=0,gpass_n=0;
  if (argc<2){fprintf(stderr,"usage: %s <posfile> [mc]\n",argv[0]);return 1;}
  init_gnugo(20.0,1); gnugo_clear_board(9); komi=7.5; set_level(10);
  if (argc>2 && !strcmp(argv[2],"mc")) { use_monte_carlo_genmove=1; choose_mc_patterns(NULL); }
  f=fopen(argv[1],"r"); if(!f){perror("open");return 1;}
  while (fscanf(f,"%d %d %3s %d",&kr,&kc,ctm,&nstones)==4) {
    int ctmc=(ctm[0]=='B')?BLACK:WHITE, resign=0, move, kpass=(kr<0), gpass;
    gnugo_clear_board(9);
    for(i=0;i<nstones;i++){ if(fscanf(f,"%d %d %3s",&r,&c,col)!=3)break; add_stone(POS(r,c),(col[0]=='B')?BLACK:WHITE); }
    move=genmove(ctmc,NULL,&resign);
    gpass=(move==PASS_MOVE);
    if(kpass)kpass_n++; if(gpass)gpass_n++;
    if((kpass&&gpass)||(!kpass&&!gpass&&I(move)==kr&&J(move)==kc)) match++;
    total++;
  }
  fclose(f);
  printf("match %d/%d = %.1f%%   (kata-pass %d, gnugo-pass %d)\n",
	 match, total, total ? 100.0 * match / total : 0.0, kpass_n, gpass_n);
  return 0;
}
