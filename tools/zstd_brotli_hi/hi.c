// zstd-hi / brotli-hi search bench.
// usage: hi <zstd|brotli> <reps> file1 file2 ...   ; configs read from stdin, one per line
// zstd config: L=19,wlog=..,clog=..,hlog=..,slog=..,mml=..,tlen=..,strat=..,ldm=0/1,lit=0/1/2,split=0/1/2
// brotli config: q=11,w=24,b=0,mode=0/1/2,nolcm=0/1,npost=..,ndir=..
// output per file: RES cfg file orig comp decode_ms(median) OK|FAIL
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#include <brotli/encode.h>
#include <brotli/decode.h>

static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e3+t.tv_nsec*1e-6;}
static int cmpd(const void*a,const void*b){double x=*(double*)a,y=*(double*)b;return x<y?-1:x>y;}
static int getv(const char*cfg,const char*key,int def){
  char k[32]; snprintf(k,sizeof k,"%s=",key); const char*p=cfg;
  while((p=strstr(p,k))){ if(p==cfg||p[-1]==','){ return atoi(p+strlen(k)); } p++; }
  return def;
}
int main(int argc,char**argv){
  int isz=strcmp(argv[1],"zstd")==0; int reps=atoi(argv[2]); int nf=argc-3;
  uint8_t**buf=malloc(nf*sizeof*buf); size_t*len=malloc(nf*sizeof*len);
  for(int i=0;i<nf;i++){FILE*f=fopen(argv[3+i],"rb");fseek(f,0,2);len[i]=ftell(f);fseek(f,0,0);buf[i]=malloc(len[i]+1);if(fread(buf[i],1,len[i],f)!=len[i])return 1;fclose(f);}
  char cfg[512];
  while(fgets(cfg,sizeof cfg,stdin)){
    cfg[strcspn(cfg,"\r\n")]=0; if(!cfg[0]) continue;
    for(int i=0;i<nf;i++){
      size_t n=len[i]; uint8_t*src=buf[i]; size_t cap; uint8_t*dst; size_t clen=0; int ok=1;
      uint8_t*out=malloc(n+1);
      if(isz){
        ZSTD_CCtx*c=ZSTD_createCCtx();
        ZSTD_CCtx_setParameter(c,ZSTD_c_compressionLevel,getv(cfg,"L",3));
        int v;
        if((v=getv(cfg,"wlog",0))) ZSTD_CCtx_setParameter(c,ZSTD_c_windowLog,v);
        if((v=getv(cfg,"clog",0))) ZSTD_CCtx_setParameter(c,ZSTD_c_chainLog,v);
        if((v=getv(cfg,"hlog",0))) ZSTD_CCtx_setParameter(c,ZSTD_c_hashLog,v);
        if((v=getv(cfg,"slog",0))) ZSTD_CCtx_setParameter(c,ZSTD_c_searchLog,v);
        if((v=getv(cfg,"mml",0))) ZSTD_CCtx_setParameter(c,ZSTD_c_minMatch,v);
        if((v=getv(cfg,"tlen",-1))>=0) ZSTD_CCtx_setParameter(c,ZSTD_c_targetLength,v);
        if((v=getv(cfg,"strat",0))) ZSTD_CCtx_setParameter(c,ZSTD_c_strategy,v);
        if((v=getv(cfg,"ldm",0))) ZSTD_CCtx_setParameter(c,ZSTD_c_enableLongDistanceMatching,v);
        if((v=getv(cfg,"lit",0))) ZSTD_CCtx_setParameter(c,ZSTD_c_literalCompressionMode,v);
        if((v=getv(cfg,"split",0))) ZSTD_CCtx_setParameter(c,ZSTD_c_splitAfterSequences,v);
        ZSTD_CCtx_setParameter(c,ZSTD_c_contentSizeFlag,1);
        ZSTD_CCtx_setParameter(c,ZSTD_c_checksumFlag,0);
        cap=ZSTD_compressBound(n); dst=malloc(cap); clen=ZSTD_compress2(c,dst,cap,src,n);
        if(ZSTD_isError(clen)){printf("ERR %s %s\n",cfg,ZSTD_getErrorName(clen)); ZSTD_freeCCtx(c); free(dst); free(out); continue;}
        ZSTD_freeCCtx(c);
      } else {
        BrotliEncoderState*e=BrotliEncoderCreateInstance(0,0,0);
        BrotliEncoderSetParameter(e,BROTLI_PARAM_QUALITY,getv(cfg,"q",11));
        BrotliEncoderSetParameter(e,BROTLI_PARAM_LGWIN,getv(cfg,"w",22));
        int v;
        if((v=getv(cfg,"b",0))) BrotliEncoderSetParameter(e,BROTLI_PARAM_LGBLOCK,v);
        BrotliEncoderSetParameter(e,BROTLI_PARAM_MODE,getv(cfg,"mode",0));
        if(getv(cfg,"nolcm",0)) BrotliEncoderSetParameter(e,BROTLI_PARAM_DISABLE_LITERAL_CONTEXT_MODELING,1);
        if((v=getv(cfg,"npost",-1))>=0) BrotliEncoderSetParameter(e,BROTLI_PARAM_NPOSTFIX,v);
        if((v=getv(cfg,"ndir",-1))>=0) BrotliEncoderSetParameter(e,BROTLI_PARAM_NDIRECT,v);
        BrotliEncoderSetParameter(e,BROTLI_PARAM_SIZE_HINT,(uint32_t)(n>(1u<<30)?(1u<<30):n));
        cap=BrotliEncoderMaxCompressedSize(n)+1024; dst=malloc(cap);
        size_t ai=n,ao=cap; const uint8_t*ni=src; uint8_t*no=dst;
        if(!BrotliEncoderCompressStream(e,BROTLI_OPERATION_FINISH,&ai,&ni,&ao,&no,0)||!BrotliEncoderIsFinished(e)){printf("ERR %s\n",cfg);BrotliEncoderDestroyInstance(e);free(dst);free(out);continue;}
        clen=cap-ao; BrotliEncoderDestroyInstance(e);
      }
      double t[64]; if(reps>64) reps=64;
      for(int r=0;r<reps;r++){
        double s=now();
        if(isz){ size_t d=ZSTD_decompress(out,n,dst,clen); if(ZSTD_isError(d)||d!=n) ok=0; }
        else { size_t d=n; if(BrotliDecoderDecompress(clen,dst,&d,out)!=BROTLI_DECODER_RESULT_SUCCESS||d!=n) ok=0; }
        t[r]=now()-s;
        if(r==0 && memcmp(out,src,n)) ok=0;
      }
      qsort(t,reps,sizeof(double),cmpd);
      const char*fn=strrchr(argv[3+i],'/'); fn=fn?fn+1:argv[3+i];
      printf("RES %s %s %zu %zu %.4f %s\n",cfg,fn,n,clen,t[reps/2],ok?"OK":"FAIL");
      fflush(stdout); free(dst); free(out);
    }
  }
  return 0;
}
