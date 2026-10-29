#include <stdio.h>
#include <stdlib.h>
static unsigned char*rd(const char*f,size_t*n){FILE*p=fopen(f,"rb");fseek(p,0,2);*n=ftell(p);rewind(p);unsigned char*b=malloc(*n+1);if(fread(b,1,*n,p)){};fclose(p);return b;}
int main(int c,char**v){size_t R=atoi(v[1]),U=atol(v[2]),nf,nl;unsigned char*F=rd(v[3],&nf),*L=rd(v[4],&nl);
 unsigned char*o=malloc(U);size_t j=0;for(size_t i=0;i<U;i++){if((F[i>>3]>>(7-(i&7)))&1)o[i]=L[j++];else o[i]=o[i-R];}
 fwrite(o,1,U,stdout);return 0;}
