// Compiled HE3 decoder (modes full/split/norev, tight or byte-aligned layout) + in-process timing vs plain zstd.
// usage: he3dec container.he3 original.bin plain.zst reps
#include <lzma.h>
static void XZ(unsigned char*o,size_t on,const unsigned char*i,size_t in){uint64_t ml=UINT64_MAX;size_t ip=0,op=0;lzma_stream_buffer_decode(&ml,0,NULL,i,&ip,in,o,&op,on);}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
static unsigned char*rd(const char*f,size_t*n){FILE*p=fopen(f,"rb");fseek(p,0,2);*n=ftell(p);rewind(p);unsigned char*b=malloc(*n+8);if(fread(b,1,*n,p)!=*n)exit(2);fclose(p);return b;}
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e3+t.tv_nsec/1e6;}
static int pc(uint64_t v){return __builtin_popcountll(v);}
/* regenerate targets in the same order as he3.py: combinations of ones in hi (S bits) then lo (J-S bits) */
static int nt; static uint64_t *T;
static void comb(int n,int k,int start,uint64_t acc,int base,void(*cb)(uint64_t)){ if(!k){cb(acc);return;} for(int i=start;i<n;i++){ comb(n,k-1,i+1,acc|(1ULL<<(base-1-i)),base,cb);} }
static int J_,S_,K_; static uint64_t curhi;
static void addlo(uint64_t lo){ if(nt<K_) T[nt++]=curhi|lo; }
static void addhi(uint64_t hi){ curhi=hi; for(int ol=0;ol<=J_-S_&&nt<K_;ol++) comb(J_-S_,ol,0,0,J_-S_,addlo); }
static void gen(int J,int S,int K){ J_=J;S_=S;K_=K;nt=0;T=malloc(sizeof(uint64_t)*K); for(int oh=0;oh<=S&&nt<K;oh++){ /* hi patterns occupy top S bits */
  /* enumerate combinations of oh ones among S high positions; position b -> bit (J-1-b) */
  int idx[64]; for(int i=0;i<oh;i++)idx[i]=i;
  while(1){ uint64_t hi=0; for(int i=0;i<oh;i++) hi|=1ULL<<(J-1-idx[i]); addhi(hi); if(nt>=K)return;
    int i=oh-1; while(i>=0&&idx[i]==S-oh+i)i--; if(i<0)break; idx[i]++; for(int j=i+1;j<oh;j++)idx[j]=idx[j-1]+1; } } }
static int cmpu(const void*a,const void*b){uint64_t x=*(uint64_t*)a,y=*(uint64_t*)b;return x<y?-1:x>y;}
typedef struct{uint64_t k,v;}KV; static int cmpkv(const void*a,const void*b){return cmpu(a,b);}
int main(int ac,char**av){
  size_t cn,on,pn; unsigned char*c=rd(av[1],&cn),*orig=rd(av[2],&on),*pz=rd(av[3],&pn); int reps=atoi(av[4]);
  int J=c[3],S=c[4],mode=c[5],lay=c[6]; int K=c[7]|c[8]<<8; uint64_t nb; memcpy(&nb,c+9,8); uint32_t l1,l2; memcpy(&l1,c+17,4); memcpy(&l2,c+21,4);
  size_t p=25; uint64_t*src=malloc(8*(K+1)); { uint64_t acc=0; int have=0; size_t q=p; for(int i=0;i<K;i++){ uint64_t v=0; for(int b=0;b<J;b++){ if(!have){acc=c[q++];have=8;} v=(v<<1)|((acc>>(have-1))&1); have--; } src[i]=v; } }
  p+=(K*(size_t)J+7)/8; uint64_t nbits=nb*8, n=nbits/J, tb=nbits-n*J; size_t tl=(tb+7)/8; unsigned char*tail=c+p; p+=tl;
  unsigned char*z1=c+p, *z2=c+p+l1;
  /* build inverse map: target->source over the completed permutation */
  gen(J,S,K); KV*inv=malloc(sizeof(KV)*2*K); int ni=0;
  /* forward map src->T, plus displaced: sorted(T\src) -> sorted(src\T) */
  uint64_t*ss=malloc(8*K),*ts=malloc(8*K); memcpy(ss,src,8*K); memcpy(ts,T,8*K); qsort(ss,K,8,cmpu); qsort(ts,K,8,cmpu);
  for(int i=0;i<K;i++){inv[ni].k=T[i];inv[ni].v=src[i];ni++;}
  { uint64_t*a=malloc(8*K),*b=malloc(8*K); int na=0,nb2=0; int i=0,j=0;
    for(i=0;i<K;i++){ if(!bsearch(&ts[i],ss,K,8,cmpu)) a[na++]=ts[i]; } for(i=0;i<K;i++){ if(!bsearch(&ss[i],ts,K,8,cmpu)) b[nb2++]=ss[i]; }
    for(i=0;i<na;i++){inv[ni].k=b[i];inv[ni].v=a[i];ni++;} }
  qsort(inv,ni,sizeof(KV),cmpkv);
  size_t s1n=(lay?(size_t)n*((S+7)/8):(n*S+7)/8), s2n=(lay?(size_t)n*((J-S+7)/8):(n*(J-S)+7)/8);
  unsigned char*s1=calloc(s1n+16,1),*s2=calloc(s2n+16,1),*out=malloc(on+16),*pout=malloc(on+16);
  double best=1e18,bestp=1e18; double tt[64],tp[64]; int ok=0;
  for(int r=0;r<reps;r++){
    double t0=now();
    XZ(s1,s1n+16,z1,l1); XZ(s2,s2n+16,z2,l2);
    /* optimized: direct inverse table + 64-bit bit readers/writers */
    uint32_t *tab=malloc(sizeof(uint32_t)<<J); for(uint64_t x=0;x<(1ULL<<J);x++) tab[x]=(uint32_t)x;
    if(mode!=1) for(int m=0;m<ni;m++) tab[inv[m].k]=(uint32_t)inv[m].v;
    memset(out,0,on+16);
    uint64_t wacc=0; int wn=0; size_t wo=0;
    #define GETBITS(buf,bitpos,cnt) ({ uint64_t _p=(bitpos), _w=0; size_t _B=_p>>3; for(int _k=0;_k<8;_k++) _w=(_w<<8)|(buf)[_B+_k]; (_w<<(_p&7))>>(64-(cnt)); })
    int hb=S, lb=J-S;
    for(uint64_t i=0;i<n;i++){
      uint64_t li=(mode==0||mode==1)? n-1-i : i, hi,lo;
      if(lay){ int b1=(S+7)/8,b2=(J-S+7)/8; hi=0;lo=0; for(int k=0;k<b1;k++) hi=(hi<<8)|s1[i*b1+k]; for(int k=0;k<b2;k++) lo=(lo<<8)|s2[li*b2+k]; }
      else { hi=hb?GETBITS(s1,i*hb,hb):0; lo=lb?GETBITS(s2,li*lb,lb):0; }
      uint64_t v=tab[(hi<<lb)|lo];
      wacc=(wacc<<J)|v; wn+=J;
      while(wn>=8){ out[wo++]=(unsigned char)(wacc>>(wn-8)); wn-=8; }
    }
    uint64_t ob=wo*8; if(wn){ out[wo]=(unsigned char)(wacc<<(8-wn)); ob+=wn; }
    for(uint64_t k=0;k<tb;k++){ if((tail[k>>3]>>(7-(k&7)))&1) out[ob>>3]|=0x80>>(ob&7); ob++; }
    free(tab);
    tt[r]=now()-t0;
    double t1=now(); XZ(pout,on+16,pz,pn); tp[r]=now()-t1;
    if(r==0) ok=!memcmp(out,orig,on)&&!memcmp(pout,orig,on);
  }
  int cmpd(const void*a,const void*b){double x=*(double*)a,y=*(double*)b;return x<y?-1:x>y;}
  qsort(tt,reps,8,cmpd); qsort(tp,reps,8,cmpd);
  printf("{\"ok\":%s,\"exchange_decode_ms\":%.2f,\"plain_xz_decode_ms\":%.2f}\n",ok?"true":"false",tt[reps/2],tp[reps/2]);
}
