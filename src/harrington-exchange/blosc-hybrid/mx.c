// Harrington Exchange -> Blosc2 mutant: full decode timing (Blosc2 decompress of hi/lo streams, recombine, inverse rename LUT, repack to J-bit packed stream).
// usage: mx hi.raw tsH flH lo.raw tsL flL inv.lut J S nsym original.bin
#include <blosc2.h>
#include <blosc2/filters-registry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e3+t.tv_nsec/1e6;}
static char*rd(const char*f,long*n){FILE*p=fopen(f,"rb");fseek(p,0,2);*n=ftell(p);rewind(p);char*b=malloc(*n+16);if(fread(b,1,*n,p)!=(size_t)*n)exit(2);fclose(p);return b;}
static char*cmp(char*in,long n,int ts,int fl,int*cs){blosc2_cparams cp=BLOSC2_CPARAMS_DEFAULTS;cp.compcode=BLOSC_ZSTD;cp.clevel=9;cp.typesize=ts;cp.nthreads=1;
 for(int i=0;i<BLOSC2_MAX_FILTERS;i++)cp.filters[i]=0; if(fl==0)cp.filters[5]=BLOSC_SHUFFLE; if(fl==1)cp.filters[5]=BLOSC_BITSHUFFLE;
 if(fl==2){cp.filters[4]=BLOSC_SHUFFLE;cp.filters[5]=BLOSC_FILTER_BYTEDELTA;cp.filters_meta[5]=ts;}
 blosc2_context*c=blosc2_create_cctx(cp);char*o=malloc(n+BLOSC2_MAX_OVERHEAD);*cs=blosc2_compress_ctx(c,in,n,o,n+BLOSC2_MAX_OVERHEAD);return o;}
static uint32_t get(char*a,int sz,long i){return sz==1?((uint8_t*)a)[i]:sz==2?((uint16_t*)a)[i]:((uint32_t*)a)[i];}
static int c8(const void*a,const void*b){double x=*(double*)a,y=*(double*)b;return x<y?-1:x>y;}
int main(int ac,char**av){blosc2_init();long nh,nl,nv,no;char*h=rd(av[1],&nh),*l=rd(av[4],&nl);uint32_t*inv=(uint32_t*)rd(av[7],&nv);char*orig=rd(av[11],&no);
 int J=atoi(av[8]),S=atoi(av[9]);long n=atol(av[10]);int sh=nh/n,sl=nl/n;int ch,cl;char*zh=cmp(h,nh,atoi(av[2]),atoi(av[3]),&ch),*zl=cmp(l,nl,atoi(av[5]),atoi(av[6]),&cl);
 blosc2_dparams dp=BLOSC2_DPARAMS_DEFAULTS;dp.nthreads=1;blosc2_context*dc=blosc2_create_dctx(dp);char*H=malloc(nh),*L=malloc(nl);uint8_t*out=calloc(no+8,1);double t[15];
 for(int r=0;r<15;r++){double s=now();blosc2_decompress_ctx(dc,zh,ch,H,nh);blosc2_decompress_ctx(dc,zl,cl,L,nl);
  uint64_t acc=0;int have=0;long p=0;
  for(long i=0;i<n;i++){uint32_t w=(get(H,sh,i)<<(J-S))|get(L,sl,i);uint32_t v=inv[w];acc=(acc<<J)|v;have+=J;while(have>=8){have-=8;out[p++]=(uint8_t)(acc>>have);}}
  t[r]=now()-s;}
 qsort(t,15,8,c8);long full=(n*J)/8;printf("%d %d %d %.3f\n",ch,cl,memcmp(out,orig,full)==0,t[7]);return 0;}
