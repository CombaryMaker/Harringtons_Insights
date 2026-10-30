// Record-length detection + delta front end benchmark (Silesia)
// For each file: detect record length d; compress plain vs delta(d) with
// gzip -9 (zlib), zstd -19, lz4 -12 (HC), xz -9e; verify exact round trip;
// time decode (decompress + reverse delta), median of N runs.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <zlib.h>
#include <lzma.h>
#include "zstd.h"
#include "lz4.h"
#include "lz4hc.h"

static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}

// Detector: for each candidate distance d in [2,MAXD], count bytes equal to byte d back
// (sampled). Pick best d; prefer smallest d whose score is within 97% of the best
// (multiples of the true length score about the same). Require a clear gain over d=1..
#define MAXD 4096
static int detect(const uint8_t*b,size_t n,double*gain){
  size_t step = n>200000? n/200000:1; // sample
  static uint64_t cnt[MAXD+1]; memset(cnt,0,sizeof cnt);
  uint64_t tot=0;
  for(size_t i=MAXD;i<n;i+=step){ tot++; for(int d=1;d<=MAXD;d++) if(b[i]==b[i-d]) cnt[d]++; }
  double base=0; // median-ish baseline: average over all d
  for(int d=2;d<=MAXD;d++) base+=cnt[d]; base/=(MAXD-1);
  int best=2; for(int d=2;d<=MAXD;d++) if(cnt[d]>cnt[best]) best=d;
  int pick=best; for(int d=2;d<best;d++) if(cnt[d]>=0.97*cnt[best] && best%d==0){pick=d;break;}
  *gain = tot? (cnt[pick]-base)/(double)tot : 0;
  return pick;
}

static void delta_enc(const uint8_t*in,uint8_t*out,size_t n,int d){
  memcpy(out,in,n<(size_t)d?n:(size_t)d);
  for(size_t i=d;i<n;i++) out[i]=in[i]-in[i-d];
}
#include <immintrin.h>
// Reverse delta. d>=16: plain 16-byte SIMD add. d=2,4,8: SIMD prefix scan with stride d.
#define SCAN(D,SET) { __m128i carry=_mm_setzero_si128(); size_t i=0; \
  for(;i+16<=n;i+=16){ __m128i x=_mm_loadu_si128((__m128i*)(b+i)); \
    if(i==0){ /* first d bytes are raw: zero them for scan then restore */ } \
    x=_mm_add_epi8(x,_mm_slli_si128(x,D)); \
    if(2*D<16) x=_mm_add_epi8(x,_mm_slli_si128(x,(2*D)&15)); \
    if(4*D<16) x=_mm_add_epi8(x,_mm_slli_si128(x,(4*D)&15)); \
    if(8*D<16) x=_mm_add_epi8(x,_mm_slli_si128(x,(8*D)&15)); \
    x=_mm_add_epi8(x,carry); _mm_storeu_si128((__m128i*)(b+i),x); carry=SET; } \
  for(;i<n;i++) b[i]+=b[i-D]; return; }
static void delta_dec(uint8_t*b,size_t n,int d){
  // encoder stored first d bytes raw and in[i]-in[i-d] after; that equals a stride-d prefix sum of all bytes
  if(n>=32){
    if(d==2) SCAN(2,_mm_set1_epi16(*(int16_t*)(b+i+14)))
    if(d==4) SCAN(4,_mm_set1_epi32(*(int32_t*)(b+i+12)))
    if(d==8) SCAN(8,_mm_set1_epi64x(*(int64_t*)(b+i+8)))
  }
  size_t i=d;
  if(d>=16){ for(;i+16<=n;i+=16){ __m128i x=_mm_loadu_si128((__m128i*)(b+i)),y=_mm_loadu_si128((__m128i*)(b+i-d)); _mm_storeu_si128((__m128i*)(b+i),_mm_add_epi8(x,y)); } }
  for(;i<n;i++) b[i]+=b[i-d];
}
typedef struct{const char*name;} C;
static int LG=9,LZ=19,LL=12,LX=9|LZMA_PRESET_EXTREME;
static size_t comp(int c,const uint8_t*in,size_t n,uint8_t**out){
  size_t cap;
  if(c==0){ uLongf l=compressBound(n); *out=malloc(l);
    z_stream s; memset(&s,0,sizeof s); deflateInit2(&s,LG,Z_DEFLATED,15,9,Z_DEFAULT_STRATEGY);
    s.next_in=(Bytef*)in; s.avail_in=n; s.next_out=*out; s.avail_out=l; deflate(&s,Z_FINISH);
    size_t r=s.total_out; deflateEnd(&s); return r; }
  if(c==1){ cap=ZSTD_compressBound(n); *out=malloc(cap); return ZSTD_compress(*out,cap,in,n,LZ); }
  if(c==2){ cap=LZ4_compressBound(n); *out=malloc(cap); return LL>0? LZ4_compress_HC((const char*)in,(char*)*out,n,cap,LL) : LZ4_compress_fast((const char*)in,(char*)*out,n,cap,-LL); }
  { cap=lzma_stream_buffer_bound(n); *out=malloc(cap); size_t pos=0;
    lzma_easy_buffer_encode(LX,LZMA_CHECK_CRC32,NULL,in,n,*out,&pos,cap); return pos; }
}
static void decomp(int c,const uint8_t*in,size_t ln,uint8_t*out,size_t n){
  if(c==0){ uLongf l=n; z_stream s; memset(&s,0,sizeof s); inflateInit2(&s,15);
    s.next_in=(Bytef*)in; s.avail_in=ln; s.next_out=out; s.avail_out=l; inflate(&s,Z_FINISH); inflateEnd(&s); return; }
  if(c==1){ ZSTD_decompress(out,n,in,ln); return; }
  if(c==2){ LZ4_decompress_safe((const char*)in,(char*)out,ln,n); return; }
  { uint64_t mem=UINT64_MAX; size_t ip=0,op=0; lzma_stream_buffer_decode(&mem,0,NULL,in,&ip,ln,out,&op,n); }
}
static int cmpd(const void*a,const void*b){double x=*(double*)a,y=*(double*)b;return x<y?-1:x>y;}

int main(int argc,char**argv){
  static char nm[4][16]; const char*names[4];
  if(getenv("FAST")){LG=1;LZ=1;LL=-1;LX=0;} 
  if(getenv("LZ")) LZ=atoi(getenv("LZ"));
  sprintf(nm[0],"gzip-%d",LG);sprintf(nm[1],"zstd-%d",LZ);sprintf(nm[2],LL>0?"lz4-%d":"lz4-fast",LL);sprintf(nm[3],"xz-%d",LX&15);for(int k=0;k<4;k++)names[k]=nm[k];
  int only=-1; if(argc>3) only=atoi(argv[3]);
  FILE*f=fopen(argv[1],"rb"); fseek(f,0,2); size_t n=ftell(f); fseek(f,0,0);
  uint8_t*b=malloc(n); fread(b,1,n,f); fclose(f);
  int reps=atoi(argv[2]);
  double gain; int d=detect(b,n,&gain);
  uint8_t*t=malloc(n); delta_enc(b,t,n,d);
  uint8_t*o=malloc(n);
  const char*fn=strrchr(argv[1],'/'); fn=fn?fn+1:argv[1];
  printf("FILE %s %zu d=%d gain=%.3f\n",fn,n,d,gain);
  for(int c=0;c<4;c++){ if(only>=0&&c!=only) continue;
    uint8_t*p,*q; size_t lp=comp(c,b,n,&p), lq=comp(c,t,n,&q);
    double tp[32],tq[32];
    for(int r=0;r<reps;r++){
      double s=now(); decomp(c,p,lp,o,n); tp[r]=now()-s; if(r==0&&memcmp(o,b,n)){printf("PLAIN FAIL\n");}
      s=now(); decomp(c,q,lq,o,n); delta_dec(o,n,d); tq[r]=now()-s; if(r==0&&memcmp(o,b,n)){printf("DELTA FAIL\n");}
    }
    qsort(tp,reps,sizeof(double),cmpd); qsort(tq,reps,sizeof(double),cmpd);
    printf("RES %s %s plain %zu %.2f delta %zu %.2f\n",fn,names[c],lp,tp[reps/2]*1e3,lq,tq[reps/2]*1e3);
    fflush(stdout); free(p); free(q);
  }
  return 0;
}
