// LZ4 default-level decode time, in-process, median of 15. usage: lz4t file
#include "lz4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e3+t.tv_nsec/1e6;}
static int c(const void*a,const void*b){double x=*(double*)a,y=*(double*)b;return x<y?-1:x>y;}
int main(int ac,char**av){FILE*f=fopen(av[1],"rb");fseek(f,0,2);int n=ftell(f);rewind(f);char*in=malloc(n);if(fread(in,1,n,f)!=(size_t)n)return 2;
 int cap=LZ4_compressBound(n);char*o=malloc(cap);int cs=LZ4_compress_default(in,o,n,cap);char*r=malloc(n);double t[15];
 for(int i=0;i<15;i++){double s=now();LZ4_decompress_safe(o,r,cs,n);t[i]=now()-s;}qsort(t,15,8,c);
 printf("%d %d %.3f\n",cs,memcmp(in,r,n)==0,t[7]);}
