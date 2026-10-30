#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <lz4frame.h>
#include <zlib.h>
static unsigned char *rd(const char*f,size_t*n){FILE*p=fopen(f,"rb");fseek(p,0,2);*n=ftell(p);rewind(p);unsigned char*b=malloc(*n);if(fread(b,1,*n,p)){};fclose(p);return b;}
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e6+t.tv_nsec/1e3;}
static int cmp(const void*a,const void*b){double x=*(double*)a,y=*(double*)b;return x<y?-1:x>y;}
static size_t dec(int k,unsigned char*in,size_t n,unsigned char*out,size_t cap){
 if(k==0){LZ4F_dctx*c;LZ4F_createDecompressionContext(&c,LZ4F_VERSION);size_t o=cap,i=n;LZ4F_decompress(c,out,&o,in,&i,0);LZ4F_freeDecompressionContext(c);return o;}
 z_stream s;memset(&s,0,sizeof s);inflateInit2(&s,31);s.next_in=in;s.avail_in=n;s.next_out=out;s.avail_out=cap;inflate(&s,Z_FINISH);size_t o=s.total_out;inflateEnd(&s);return o;}
int main(int ac,char**av){const char*f=av[1];int R=atoi(av[2]);char p[256];size_t U;sprintf(p,"../cant_dl/canterbury-corpus/canterbury/%s",f);unsigned char*orig=rd(p,&U);
 unsigned char*out=malloc(U),*fl=malloc(U/8+16),*li=malloc(U);const char*ext[]={"lz4","gz"};
 for(int k=0;k<2;k++){size_t na,nf,nl;sprintf(p,"%s.%s",f,ext[k]);unsigned char*A=rd(p,&na);sprintf(p,"%s.flags.%s",f,ext[k]);unsigned char*F=rd(p,&nf);sprintf(p,"%s.lits.%s",f,ext[k]);unsigned char*L=rd(p,&nl);
  double ta[41],tb[41];
  for(int r=0;r<41;r++){double t=now();dec(k,A,na,out,U);ta[r]=now()-t;
   t=now();dec(k,F,nf,fl,U/8+16);dec(k,L,nl,li,U);size_t j=0;
   size_t i=0;for(;i+8<=U;i+=8){unsigned m=fl[i>>3];
     if(m==0){ if(R>=8) memcpy(out+i,out+i-R,8); else for(int q=0;q<8;q++)out[i+q]=out[i+q-R]; }
     else if(m==255){memcpy(out+i,li+j,8);j+=8;}
     else for(int q=0;q<8;q++){if((m>>(7-q))&1)out[i+q]=li[j++];else out[i+q]=out[i+q-R];}}
   for(;i<U;i++){if((fl[i>>3]>>(7-(i&7)))&1)out[i]=li[j++];else out[i]=out[i-R];}
   tb[r]=now()-t;}
  if(memcmp(out,orig,U)){printf("MISMATCH\n");return 1;}
  qsort(ta,41,8,cmp);qsort(tb,41,8,cmp);printf("%s %s plain %zu B %.0f us | activity %zu B %.0f us\n",f,ext[k],na,ta[20],nf+nl,tb[20]);}
}
