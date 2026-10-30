// LZMA (7-Zip SDK) and xz (liblzma) head-to-head bench.
// usage:
//   lz lzma <reps> file...      configs on stdin: lvl=9,lc=3,lp=0,pb=2,fb=..,mc=..,dict=..(log2, 0=default),reduce=0/1
//   lz xzdec <reps> orig compressed.xz   -> times liblzma decode of an existing .xz
// output: RES cfg file orig comp decode_ms OK|FAIL
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <lzma.h>
#include "LzmaEnc.h"
#include "LzmaDec.h"
#include "Alloc.h"

static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e3+t.tv_nsec*1e-6;}
static int cmpd(const void*a,const void*b){double x=*(double*)a,y=*(double*)b;return x<y?-1:x>y;}
static int getv(const char*cfg,const char*key,int def){char k[32];snprintf(k,sizeof k,"%s=",key);const char*p=cfg;
  while((p=strstr(p,k))){if(p==cfg||p[-1]==',')return atoi(p+strlen(k));p++;}return def;}
static uint8_t*rd(const char*fn,size_t*n){FILE*f=fopen(fn,"rb");fseek(f,0,2);*n=ftell(f);fseek(f,0,0);uint8_t*b=malloc(*n+1);if(fread(b,1,*n,f)!=*n)exit(1);fclose(f);return b;}
static void*SzAlloc(ISzAllocPtr p,size_t s){(void)p;return malloc(s);}
static void SzFree(ISzAllocPtr p,void*a){(void)p;free(a);}
static const ISzAlloc my_Alloc={SzAlloc,SzFree};

int main(int argc,char**argv){
  int reps=atoi(argv[2]); if(reps>64)reps=64; double t[64];
  if(!strcmp(argv[1],"xzdec")){
    size_t n,cn; uint8_t*o=rd(argv[3],&n),*c=rd(argv[4],&cn),*d=malloc(n+1); int ok=1;
    for(int r=0;r<reps;r++){uint64_t mem=UINT64_MAX;size_t ip=0,op=0;double s=now();
      if(lzma_stream_buffer_decode(&mem,0,NULL,c,&ip,cn,d,&op,n)!=LZMA_OK||op!=n)ok=0; t[r]=now()-s;
      if(r==0&&memcmp(d,o,n))ok=0;}
    qsort(t,reps,sizeof(double),cmpd);
    const char*fn=strrchr(argv[3],'/');fn=fn?fn+1:argv[3];
    printf("RES xz:%s %s %zu %zu %.4f %s\n",argc>5?argv[5]:"file",fn,n,cn,t[reps/2],ok?"OK":"FAIL"); return 0;
  }
  int nf=argc-3; char cfg[512];
  uint8_t**buf=malloc(nf*sizeof*buf); size_t*len=malloc(nf*sizeof*len);
  for(int i=0;i<nf;i++) buf[i]=rd(argv[3+i],&len[i]);
  while(fgets(cfg,sizeof cfg,stdin)){
    cfg[strcspn(cfg,"\r\n")]=0; if(!cfg[0])continue;
    for(int i=0;i<nf;i++){
      size_t n=len[i]; CLzmaEncProps p; LzmaEncProps_Init(&p); p.level=getv(cfg,"lvl",9);
      int v;
      if((v=getv(cfg,"lc",-1))>=0)p.lc=v; if((v=getv(cfg,"lp",-1))>=0)p.lp=v; if((v=getv(cfg,"pb",-1))>=0)p.pb=v;
      if((v=getv(cfg,"fb",0)))p.fb=v; if((v=getv(cfg,"mc",0)))p.mc=v; if((v=getv(cfg,"dict",0)))p.dictSize=1u<<v;
      if(getv(cfg,"reduce",0)) p.reduceSize=n;
      p.numThreads=1;
      size_t cap=n+n/3+(1<<16); uint8_t*c=malloc(cap); size_t cl=cap; uint8_t props[LZMA_PROPS_SIZE]; size_t ps=LZMA_PROPS_SIZE;
      SRes res=LzmaEncode(c,&cl,buf[i],n,&p,props,&ps,0,NULL,&my_Alloc,&my_Alloc);
      if(res!=SZ_OK){printf("ERR %s %d\n",cfg,res);free(c);continue;}
      uint8_t*d=malloc(n+1); int ok=1;
      for(int r=0;r<reps;r++){SizeT dl=n,sl=cl; ELzmaStatus st; double s=now();
        SRes rr=LzmaDecode(d,&dl,c,&sl,props,LZMA_PROPS_SIZE,LZMA_FINISH_END,&st,&my_Alloc); t[r]=now()-s;
        if(rr!=SZ_OK||dl!=n)ok=0; if(r==0&&memcmp(d,buf[i],n))ok=0;}
      qsort(t,reps,sizeof(double),cmpd);
      const char*fn=strrchr(argv[3+i],'/');fn=fn?fn+1:argv[3+i];
      // compressed size counts the 5-byte props header + 8-byte size (as in a .lzma file)
      printf("RES %s %s %zu %zu %.4f %s\n",cfg,fn,n,cl+13,t[reps/2],ok?"OK":"FAIL"); fflush(stdout);
      free(c);free(d);
    }
  }
  return 0;
}
