#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <random>
#include "tables.inc"
#include "mathport.inc"
int main(){
  long bad=0, n=0;
  for(int p=1;p<4096;p++){ double x=(double)p/(double)(4096-p); if(hcm_log(x)!=log(x)) bad++; n++; }
  printf("log: %ld of %ld differ\n",bad,n);
  std::mt19937_64 g(1); std::uniform_real_distribution<double> U(-20.0,20.0);
  bad=0; long N=300000000;
  for(long i=0;i<N;i++){ double x=U(g); if(hcm_exp(x)!=exp(x)) { if(bad<5) printf("  exp mismatch x=%a mine=%a libm=%a\n",x,hcm_exp(x),exp(x)); bad++; } }
  printf("exp: %ld of %ld differ\n",bad,N);
  // also check squash-like inputs near 0 and exactly-representable steps
  bad=0; for(int j=-40000;j<=40000;j++){ double x=j/2000.0; if(hcm_exp(x)!=exp(x)) bad++; }
  printf("exp grid: %ld differ\n",bad);
}
