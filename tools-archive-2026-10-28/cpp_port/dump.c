#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "aor/math/math_config.h"
static uint64_t u(double d){uint64_t x; memcpy(&x,&d,8); return x;}
int main(void){
  const struct exp_data *e=&__exp_data;
  printf("// exp constants (N=128, poly order 5)\n");
  printf("static const uint64_t EXP_INVLN2N=0x%016llxULL, EXP_NEGLN2HIN=0x%016llxULL, EXP_NEGLN2LON=0x%016llxULL, EXP_SHIFT=0x%016llxULL;\n",
    (unsigned long long)u(e->invln2N),(unsigned long long)u(e->negln2hiN),(unsigned long long)u(e->negln2loN),(unsigned long long)u(e->shift));
  printf("static const uint64_t EXP_POLY[4]={");
  for(int i=0;i<4;i++) printf("0x%016llxULL%s",(unsigned long long)u(e->poly[i]),i<3?",":"};\n");
  printf("static const uint64_t EXP_TAB[256]={\n");
  for(int i=0;i<256;i++) printf("%s0x%016llxULL%s",i%4==0?"  ":"",(unsigned long long)e->tab[i],i==255?"\n};\n":(i%4==3?",\n":","));
  const struct log_data *l=&__log_data;
  printf("// log constants (N=128, poly orders 6 and 12, no-FMA path)\n");
  printf("static const uint64_t LOG_LN2HI=0x%016llxULL, LOG_LN2LO=0x%016llxULL;\n",(unsigned long long)u(l->ln2hi),(unsigned long long)u(l->ln2lo));
  printf("static const uint64_t LOG_A[5]={");
  for(int i=0;i<5;i++) printf("0x%016llxULL%s",(unsigned long long)u(l->poly[i]),i<4?",":"};\n");
  printf("static const uint64_t LOG_B[11]={\n  ");
  for(int i=0;i<11;i++) printf("0x%016llxULL%s",(unsigned long long)u(l->poly1[i]),i<10?(i%4==3?",\n  ":","):"};\n");
  printf("static const uint64_t LOG_TAB[512]={ // invc, logc, chi, clo\n");
  for(int i=0;i<128;i++) printf("  0x%016llxULL,0x%016llxULL,0x%016llxULL,0x%016llxULL%s",
    (unsigned long long)u(l->tab[i].invc),(unsigned long long)u(l->tab[i].logc),(unsigned long long)u(l->tab2[i].chi),(unsigned long long)u(l->tab2[i].clo),i==127?"\n};\n":",\n");
  return 0;
}
