// Order-0 static rANS coder at symbol width J (1..16). One run: compress, decompress, verify.
// Output size counts: header (J, n, tail bits, nonzero symbol list + 16-bit freqs) + rANS stream.
// usage: o0 file J
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#define SB 16
#define L (1u<<23)
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e3+t.tv_nsec/1e6;}
int main(int ac,char**av){FILE*f=fopen(av[1],"rb");fseek(f,0,2);long nb=ftell(f);rewind(f);uint8_t*in=malloc(nb+8);if(fread(in,1,nb,f)!=(size_t)nb)return 2;
 int J=atoi(av[2]);long n=nb*8/J;uint32_t A=1u<<J;
 double t0=now();
 uint16_t*s=malloc(2*n);{uint64_t acc=0;int h=0;long p=0;for(long i=0;i<n;i++){while(h<J){acc=(acc<<8)|in[p++];h+=8;}s[i]=(acc>>(h-J))&(A-1);h-=J;}}
 uint32_t*cnt=calloc(A,4),*fr=calloc(A,4),*cf=calloc(A+1,4);for(long i=0;i<n;i++)cnt[s[i]]++;
 // normalise to 2^SB, every used symbol >=1
 {uint64_t tot=0;int used=0;for(uint32_t a=0;a<A;a++)if(cnt[a]){fr[a]=(uint32_t)((uint64_t)cnt[a]*(1u<<SB)/n);if(!fr[a])fr[a]=1;tot+=fr[a];used++;}
  while(tot!=(1u<<SB)){uint32_t b=0;for(uint32_t a=0;a<A;a++)if(fr[a]>fr[b])b=a; if(tot>(1u<<SB)){fr[b]--;tot--;}else{fr[b]++;tot++;}}}
 for(uint32_t a=0;a<A;a++)cf[a+1]=cf[a]+fr[a];
 uint8_t*buf=malloc(n*3+64),*end=buf+n*3+64,*ptr=end;uint32_t x=L;
 for(long i=n-1;i>=0;i--){uint32_t a=s[i],F=fr[a];uint32_t xm=((L>>SB)<<8)*F;while(x>=xm){*--ptr=x&255;x>>=8;}x=((x/F)<<SB)+(x%F)+cf[a];}
 ptr-=4;memcpy(ptr,&x,4);long body=end-ptr;
 long used=0;for(uint32_t a=0;a<A;a++)if(fr[a])used++;
 long hdr=1+8+1+2+used*((J+7)/8+2);long tailb=(nb*8-n*(long)J+7)/8;long total=hdr+body+tailb;
 double tc=now()-t0;
 // decode (table build + rANS + repack), timed
 uint8_t*out=calloc(nb+8,1);double t1=now();
 uint16_t*slot=malloc(2u<<SB);for(uint32_t a=0;a<A;a++)for(uint32_t k=cf[a];k<cf[a+1];k++)slot[k]=a;
 uint8_t*q=ptr;uint32_t y;memcpy(&y,q,4);q+=4;uint64_t acc=0;int h=0;long op=0;
 for(long i=0;i<n;i++){uint32_t m=y&((1u<<SB)-1),a=slot[m];y=fr[a]*(y>>SB)+m-cf[a];while(y<L)y=(y<<8)|*q++;acc=(acc<<J)|a;h+=J;while(h>=8){h-=8;out[op++]=acc>>h;}}
 double td=now()-t1;
 int ok=memcmp(out,in,(n*J)/8)==0;
 printf("%ld %ld %d %.2f %.3f\n",nb,total,ok,tc,td);return 0;}
