// Reversible pre-filters for HCM tests.
//   pre e8e9 enc|dec IN OUT     x86 call/jump address transform (E8/E9 + 4-byte relative -> absolute)
//   pre act  enc|dec IN OUT [R] Combary activity map: flag 1 = byte differs from the byte R back,
//                               output = header(R, n) + packed flags + literals of flagged bytes
// prints elapsed ms of the transform itself
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e3+t.tv_nsec*1e-6;}
static uint8_t*rd(const char*fn,size_t*n){FILE*f=fopen(fn,"rb");if(!f){perror(fn);exit(1);}fseek(f,0,2);*n=ftell(f);fseek(f,0,0);uint8_t*b=malloc(*n+16);if(fread(b,1,*n,f)!=*n)exit(1);fclose(f);return b;}
static void wr(const char*fn,const uint8_t*b,size_t n){FILE*f=fopen(fn,"wb");fwrite(b,1,n,f);fclose(f);}
int main(int c,char**v){
  size_t n; uint8_t*b=rd(v[3],&n); double s=now();
  if(!strcmp(v[1],"e8e9")){
    int enc=!strcmp(v[2],"enc");
    for(size_t i=0;i+5<=n;){
      if(b[i]==0xE8||b[i]==0xE9){
        int32_t a; memcpy(&a,b+i+1,4);
        a = enc ? a+(int32_t)i : a-(int32_t)i;
        memcpy(b+i+1,&a,4); i+=5;
      } else i++;
    }
    double t=now()-s; wr(v[4],b,n); printf("%.3f\n",t); return 0;
  }
  if(!strcmp(v[2],"enc")){
    uint32_t R=atoi(v[5]); size_t fb=(n+7)/8; uint8_t*o=calloc(8+fb+n,1); size_t lit=8+fb;
    memcpy(o,&R,4); uint32_t nn=(uint32_t)n; memcpy(o+4,&nn,4);
    for(size_t i=0;i<n;i++){ int fl = i<R || b[i]!=b[i-R]; if(fl){o[8+i/8]|=1<<(i&7); o[lit++]=b[i];} }
    double t=now()-s; wr(v[4],o,lit); printf("%.3f\n",t); return 0;
  } else {
    uint32_t R,nn; memcpy(&R,b,4); memcpy(&nn,b+4,4); size_t fb=(nn+7)/8; const uint8_t*fl=b+8,*lp=b+8+fb;
    uint8_t*o=malloc(nn+1);
    for(size_t i=0;i<nn;i++) o[i] = (fl[i>>3]>>(i&7))&1 ? *lp++ : o[i-R];
    double t=now()-s; wr(v[4],o,nn); printf("%.3f\n",t); return 0;
  }
}
