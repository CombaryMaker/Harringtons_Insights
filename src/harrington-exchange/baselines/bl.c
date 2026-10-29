// blosc2 baseline: bl <file> <typesize> <filter: 0=shuffle 1=bitshuffle 2=shuffle+bytedelta 3=none>
#include <blosc2.h>
#include <blosc2/filters-registry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e3+t.tv_nsec/1e6;}
int main(int c,char**v){FILE*f=fopen(v[1],"rb");fseek(f,0,2);long n=ftell(f);rewind(f);char*in=malloc(n);fread(in,1,n,f);
 int ts=atoi(v[2]),fl=atoi(v[3]);blosc2_init();
 blosc2_cparams cp=BLOSC2_CPARAMS_DEFAULTS;cp.compcode=BLOSC_ZSTD;cp.clevel=9;cp.typesize=ts;cp.nthreads=1;cp.blocksize=0;
 for(int i=0;i<BLOSC2_MAX_FILTERS;i++)cp.filters[i]=0;
 if(fl==0)cp.filters[5]=BLOSC_SHUFFLE; if(fl==1)cp.filters[5]=BLOSC_BITSHUFFLE;
 if(fl==2){cp.filters[4]=BLOSC_SHUFFLE;cp.filters[5]=BLOSC_FILTER_BYTEDELTA;cp.filters_meta[5]=ts;}
 blosc2_context*cc=blosc2_create_cctx(cp);char*out=malloc(n+BLOSC2_MAX_OVERHEAD);
 int cs=blosc2_compress_ctx(cc,in,n,out,n+BLOSC2_MAX_OVERHEAD);
 blosc2_dparams dp=BLOSC2_DPARAMS_DEFAULTS;dp.nthreads=1;blosc2_context*dc=blosc2_create_dctx(dp);char*r=malloc(n);
 double best=1e9;for(int k=0;k<5;k++){double t=now();blosc2_decompress_ctx(dc,out,cs,r,n);t=now()-t;if(t<best)best=t;}
 printf("%d %d %.3f\n",cs,memcmp(in,r,n)==0,best);return 0;}
