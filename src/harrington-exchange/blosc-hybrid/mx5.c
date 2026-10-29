// Exchange->Blosc2 mutant decoder v4: per-stream Blosc2 codec/blocksize, fused inverse-rename + repack,
// small LUT for J<=16, cache-resident hash for J>16 (only remapped symbols stored; others pass through).
// usage: mx4 J S nsym inv.lut original.bin  H ts fl codec bs  [L ts fl codec bs]   (S=0: fast mode, one stream)
#include <blosc2.h>
#include <blosc2/filters-registry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e3+t.tv_nsec/1e6;}
static char*rd(const char*f,long*n){FILE*p=fopen(f,"rb");fseek(p,0,2);*n=ftell(p);rewind(p);char*b=malloc(*n+16);if(fread(b,1,*n,p)!=(size_t)*n)exit(2);fclose(p);return b;}
static char*cmp(char*in,long n,char**a,int*cs){blosc2_cparams cp=BLOSC2_CPARAMS_DEFAULTS;int ts=atoi(a[1]),fl=atoi(a[2]);cp.compcode=atoi(a[3]);cp.blocksize=atoi(a[4]);cp.clevel=9;cp.typesize=ts;cp.nthreads=1;
 for(int i=0;i<BLOSC2_MAX_FILTERS;i++)cp.filters[i]=0; if(fl==0)cp.filters[5]=BLOSC_SHUFFLE; if(fl==1)cp.filters[5]=BLOSC_BITSHUFFLE;
 if(fl==2){cp.filters[4]=BLOSC_SHUFFLE;cp.filters[5]=BLOSC_FILTER_BYTEDELTA;cp.filters_meta[5]=ts;}
 blosc2_context*c=blosc2_create_cctx(cp);char*o=malloc(n+BLOSC2_MAX_OVERHEAD);*cs=blosc2_compress_ctx(c,in,n,o,n+BLOSC2_MAX_OVERHEAD);return o;}
static int c8(const void*a,const void*b){double x=*(double*)a,y=*(double*)b;return x<y?-1:x>y;}
#define HB 4096
static uint32_t hk[HB],hv[HB];static inline uint32_t hl(uint32_t w){uint32_t h=(w*2654435761u)>>20;while(hk[h]!=0xffffffffu){if(hk[h]==w)return hv[h];h=(h+1)&(HB-1);}return w;}
int main(int ac,char**av){blosc2_init();int J=atoi(av[1]),S=atoi(av[2]);long n=atol(av[3]),nv,no,nh,nl=0;uint32_t*inv=(uint32_t*)rd(av[4],&nv);char*orig=rd(av[5],&no);
 char*h=rd(av[6],&nh),*l=NULL;int ch,cl=0;char*zh=cmp(h,nh,av+6,&ch),*zl=NULL; if(S){l=rd(av[11],&nl);zl=cmp(l,nl,av+11,&cl);}
 int sh=nh/n,sl=S?nl/n:0,sft=J-S; uint16_t*i16=malloc(2*(nv/4)+2);for(long i=0;i<nv/4;i++)i16[i]=inv[i];
 memset(hk,0xff,sizeof hk); for(uint32_t w=0;w<nv/4;w++) if(inv[w]!=w){uint32_t x=(w*2654435761u)>>20;while(hk[x]!=0xffffffffu)x=(x+1)&(HB-1);hk[x]=w;hv[x]=inv[w];}
 blosc2_dparams dp=BLOSC2_DPARAMS_DEFAULTS;dp.nthreads=1;blosc2_context*dc=blosc2_create_dctx(dp);char*H=malloc(nh+8),*L=malloc(nl+8);uint8_t*out=calloc(no+16,1);double t[21];uint32_t*vbuf=malloc(4*n+64);
 for(int r=0;r<21;r++){double s0=now();blosc2_decompress_ctx(dc,zh,ch,H,nh);if(S)blosc2_decompress_ctx(dc,zl,cl,L,nl);
  const uint8_t*H8=(uint8_t*)H,*L8=(uint8_t*)L;const uint16_t*H16=(uint16_t*)H,*L16=(uint16_t*)L;const uint32_t*H32=(uint32_t*)H;long p=0;
  uint32_t*V=vbuf;
  /* stage 1: rebuild renamed symbol and invert (explicit loops so the compiler vectorises with gathers) */
  if(S==0){ if(sh==1)for(long i=0;i<n;i++)V[i]=inv[H8[i]]; else if(sh==2)for(long i=0;i<n;i++)V[i]=inv[H16[i]]; else for(long i=0;i<n;i++)V[i]=inv[H32[i]]; }
  else if(sh==1&&sl==1)for(long i=0;i<n;i++)V[i]=inv[((uint32_t)H8[i]<<sft)|L8[i]];
  else if(sh==1&&sl==2)for(long i=0;i<n;i++)V[i]=inv[((uint32_t)H8[i]<<sft)|L16[i]];
  else if(sh==2&&sl==1)for(long i=0;i<n;i++)V[i]=inv[((uint32_t)H16[i]<<sft)|L8[i]];
  else for(long i=0;i<n;i++)V[i]=inv[((uint32_t)H16[i]<<sft)|L16[i]];
  /* stage 2: repack */
  if(J==8){for(long i=0;i<n;i++)out[i]=V[i];}
  else if(J==16){for(long i=0;i<n;i++){uint16_t v=__builtin_bswap16((uint16_t)V[i]);memcpy(out+2*i,&v,2);}}
  else if(J==12){for(long i=0;i+1<n;i+=2){uint32_t x=(V[i]<<12)|V[i+1];out[p]=x>>16;out[p+1]=x>>8;out[p+2]=x;p+=3;}}
  else if(J==5){for(long i=0;i+7<n;i+=8){uint64_t x=0;for(int k=0;k<8;k++)x=(x<<5)|V[i+k];out[p]=x>>32;out[p+1]=x>>24;out[p+2]=x>>16;out[p+3]=x>>8;out[p+4]=x;p+=5;}}
  else {uint64_t acc=0;int have=0;for(long i=0;i<n;i++){acc=(acc<<J)|V[i];have+=J;if(have>=32){have-=32;uint32_t x=__builtin_bswap32((uint32_t)(acc>>have));memcpy(out+p,&x,4);p+=4;}}while(have>=8){have-=8;out[p++]=acc>>have;}}
  t[r]=now()-s0;}
 qsort(t,21,8,c8);long chk=(n*J)/8-4;printf("%d %d %d %.3f\n",ch,cl,memcmp(out,orig,chk)==0,t[10]);return 0;}
