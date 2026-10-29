#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emmintrin.h>
int main(int c,char**v){size_t R=atoi(v[1]);size_t cap=1<<20,n=0;unsigned char*t=malloc(cap);size_t k;
 while((k=fread(t+n,1,cap-n,stdin))>0){n+=k;if(n==cap){cap*=2;t=realloc(t,cap);}}
 unsigned char*o=malloc(n+16); size_t m=R<n?R:n; memcpy(o,t,m); size_t i=R, st=R<16?R:16;
 for(;i+16<=n;i+=st){__m128i a=_mm_loadu_si128((void*)(t+i)),b=_mm_loadu_si128((void*)(o+i-R));_mm_storeu_si128((void*)(o+i),_mm_add_epi8(a,b));}
 for(;i<n;i++)o[i]=t[i]+o[i-R]; fwrite(o,1,n,stdout);return 0;}
