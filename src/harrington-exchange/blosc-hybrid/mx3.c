// Exchange->Blosc2 mutant decoder v3: fast mode (rename only, one Blosc2 stream) or max mode (rename+split, two streams).
// Specialised repackers for J=5,12,16; generic 64-bit accumulator otherwise.
// usage: mx3 J S nsym inv.lut original.bin  hi.raw tsH flH [lo.raw tsL flL]   (S=0 => fast mode, hi.raw holds whole renamed symbol)
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
static int c8(const void*a,const void*b){double x=*(double*)a,y=*(double*)b;return x<y?-1:x>y;}
static inline uint32_t g(const char*a,int sz,long i){return sz==1?((const uint8_t*)a)[i]:sz==2?((const uint16_t*)a)[i]:((const uint32_t*)a)[i];}
int main(int ac,char**av){blosc2_init();int J=atoi(av[1]),S=atoi(av[2]);long n=atol(av[3]),nv,no,nh,nl=0;uint32_t*inv=(uint32_t*)rd(av[4],&nv);char*orig=rd(av[5],&no);
 char*h=rd(av[6],&nh),*l=NULL;int ch,cl=0;char*zh=cmp(h,nh,atoi(av[7]),atoi(av[8]),&ch),*zl=NULL;
 if(S){l=rd(av[9],&nl);zl=cmp(l,nl,atoi(av[10]),atoi(av[11]),&cl);} int sh=nh/n,sl=S?nl/n:0,sft=J-S;
 uint16_t*inv16=malloc(2*(nv/4));for(long i=0;i<nv/4;i++)inv16[i]=inv[i];
 blosc2_dparams dp=BLOSC2_DPARAMS_DEFAULTS;dp.nthreads=1;blosc2_context*dc=blosc2_create_dctx(dp);char*H=malloc(nh),*L=malloc(nl+1);uint8_t*out=calloc(no+16,1);uint32_t*v=malloc(4*n+16);double t[15];
 for(int r=0;r<15;r++){double s0=now();blosc2_decompress_ctx(dc,zh,ch,H,nh);if(S)blosc2_decompress_ctx(dc,zl,cl,L,nl);
  if(J<=16){ if(!S){ if(sh==1)for(long i=0;i<n;i++)v[i]=inv16[((uint8_t*)H)[i]]; else for(long i=0;i<n;i++)v[i]=inv16[((uint16_t*)H)[i]]; }
             else if(sh==1&&sl==1)for(long i=0;i<n;i++)v[i]=inv16[(((uint8_t*)H)[i]<<sft)|((uint8_t*)L)[i]];
             else for(long i=0;i<n;i++)v[i]=inv16[(g(H,sh,i)<<sft)|g(L,sl,i)]; }
  else { if(!S)for(long i=0;i<n;i++)v[i]=inv[g(H,sh,i)]; else for(long i=0;i<n;i++)v[i]=inv[(g(H,sh,i)<<sft)|g(L,sl,i)]; }
  long p=0;
  if(J==16){for(long i=0;i<n;i++){out[2*i]=v[i]>>8;out[2*i+1]=v[i];}p=2*n;}
  else if(J==12){long i=0;for(;i+1<n;i+=2){uint32_t a=v[i],b=v[i+1];out[p]=a>>4;out[p+1]=(a<<4)|(b>>8);out[p+2]=b;p+=3;}}
  else if(J==5){long i=0;for(;i+7<n;i+=8){uint64_t x=0;for(int k=0;k<8;k++)x=(x<<5)|v[i+k];out[p]=x>>32;out[p+1]=x>>24;out[p+2]=x>>16;out[p+3]=x>>8;out[p+4]=x;p+=5;}}
  else {uint64_t acc=0;int have=0;for(long i=0;i<n;i++){acc=(acc<<J)|v[i];have+=J;if(have>=32){have-=32;uint32_t x=__builtin_bswap32((uint32_t)(acc>>have));memcpy(out+p,&x,4);p+=4;}}while(have>=8){have-=8;out[p++]=acc>>have;}}
  t[r]=now()-s0;}
 qsort(t,15,8,c8);long full=(n*J)/8;long chk=full-4;printf("%d %d %d %.3f\n",ch,cl,memcmp(out,orig,chk)==0,t[7]);return 0;}
