#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zstd.h>
#include <lz4.h>
#include <lz4frame.h>
#include <zlib.h>
#include <emmintrin.h>
unsigned char*tmp;static void undelta(unsigned char*o,size_t U){memcpy(o,tmp,13);size_t i=13;for(;i+16<=U;i+=13){__m128i a=_mm_loadu_si128((void*)(tmp+i)),b=_mm_loadu_si128((void*)(o+i-13));_mm_storeu_si128((void*)(o+i),_mm_add_epi8(a,b));}for(;i<U;i++)o[i]=tmp[i]+o[i-13];}
static unsigned char *rd(const char*f,size_t*n){FILE*p=fopen(f,"rb");fseek(p,0,2);*n=ftell(p);rewind(p);unsigned char*b=malloc(*n);fread(b,1,*n,p);fclose(p);return b;}
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e6+t.tv_nsec/1e3;}
static int cmp(const void*a,const void*b){double x=*(double*)a,y=*(double*)b;return x<y?-1:x>y;}
size_t U; unsigned char *out;
static void dec(int k,unsigned char*in,size_t n){
 if(k==0) ZSTD_decompress(out,U,in,n);
 else if(k==1){LZ4F_dctx*c;LZ4F_createDecompressionContext(&c,LZ4F_VERSION);size_t o=U,i=n;LZ4F_decompress(c,out,&o,in,&i,0);LZ4F_freeDecompressionContext(c);}
 else {z_stream s;memset(&s,0,sizeof s);inflateInit2(&s,31);s.next_in=in;s.avail_in=n;s.next_out=out;s.avail_out=U;inflate(&s,Z_FINISH);inflateEnd(&s);}
}
int main(){const char*ext[]={"zst","lz4","gz"};size_t n;unsigned char*orig=rd("../cant_dl/canterbury-corpus/canterbury/kennedy.xls",&U);out=malloc(U);tmp=malloc(U);
 for(int k=0;k<3;k++){char a[64],b[64];sprintf(a,"kennedy.xls.%s",ext[k]);sprintf(b,"d.bin.%s",ext[k]);size_t na,nb;unsigned char*A=rd(a,&na),*B=rd(b,&nb);
  double ta[41],tb[41];
  for(int r=0;r<41;r++){double t=now();dec(k,A,na);ta[r]=now()-t;
   t=now();{unsigned char*sv=out;out=tmp;dec(k,B,nb);out=sv;}undelta(out,U);tb[r]=now()-t;}
  if(memcmp(out,orig,U)){printf("MISMATCH\n");return 1;}
  qsort(ta,41,8,cmp);qsort(tb,41,8,cmp);printf("%s plain %zu B %.0f us | delta %zu B %.0f us\n",ext[k],na,ta[20],nb,tb[20]);}
}
