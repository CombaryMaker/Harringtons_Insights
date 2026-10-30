// Timing test of the Harrington shrinking (declining) header: a permutation of N values stored with
// ceil(log2(remaining)) bits per entry. Compares raw storage, a naive decoder (remove from a list)
// and a Fenwick-tree decoder (O(N log N)). MIT License, Michael Hugh Harrington, 2026.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static int bits(uint32_t r){int b=0;while((1u<<b)<r)b++;return b;}          // ceil(log2 r)
typedef struct{uint8_t*p;size_t bit;}BW;
static void put(BW*w,uint32_t v,int nb){for(int i=nb-1;i>=0;i--){if((v>>i)&1)w->p[w->bit>>3]|=0x80>>(w->bit&7);w->bit++;}}
static uint32_t get(BW*w,int nb){uint32_t v=0;for(int i=0;i<nb;i++){v=(v<<1)|((w->p[w->bit>>3]>>(7-(w->bit&7)))&1);w->bit++;}return v;}
// Fenwick tree over "still unused" flags
static void fw_add(int*t,int n,int i,int d){for(i++;i<=n;i+=i&-i)t[i]+=d;}
static int fw_find(int*t,int n,int k){int pos=0,lg=1;while(lg*2<=n)lg*=2;for(;lg;lg>>=1)if(pos+lg<=n&&t[pos+lg]<=k){pos+=lg;k-=t[pos];}return pos;} // k-th unused (0-based)
static int fw_rank(int*t,int i){int s=0;for(;i>0;i-=i&-i)s+=t[i];return s;}             // unused before i
int main(int argc,char**argv){
  int Ns[]={16,256,4096,65536};
  printf("N       header_bytes raw_bytes  encode_ms  decode_naive_ms  decode_fenwick_ms  raw_copy_ms\n");
  for(int q=0;q<4;q++){int N=Ns[q];
    uint32_t*perm=malloc(N*4),*out=malloc(N*4);for(int i=0;i<N;i++)perm[i]=i;
    srand(12345);for(int i=N-1;i>0;i--){int j=rand()%(i+1);uint32_t t=perm[i];perm[i]=perm[j];perm[j]=t;}
    size_t cap=(size_t)N*4+16;uint8_t*buf=calloc(cap,1);int*t=calloc(N+1,4);
    int reps=N<=256?20000:(N<=4096?200:5);
    // encode (Fenwick ranks)
    double t0=now();BW w;
    for(int r=0;r<reps;r++){memset(buf,0,cap);memset(t,0,(N+1)*4);for(int i=0;i<N;i++)fw_add(t,N,i,1);
      w.p=buf;w.bit=0;for(int i=0;i<N;i++){int rem=N-i;int idx=fw_rank(t,perm[i]);put(&w,idx,bits(rem));fw_add(t,N,perm[i],-1);}}
    double enc=(now()-t0)/reps*1e3;size_t hb=(w.bit+7)/8;
    // decode naive: list of remaining values, remove by memmove
    uint32_t*lst=malloc(N*4);int nreps=N>=65536?1:reps;
    t0=now();for(int r=0;r<nreps;r++){for(int i=0;i<N;i++)lst[i]=i;BW d={buf,0};int rem=N;
      for(int i=0;i<N;i++){uint32_t idx=get(&d,bits(rem));out[i]=lst[idx];memmove(lst+idx,lst+idx+1,(rem-idx-1)*4);rem--;}}
    double dn=(now()-t0)/nreps*1e3;int ok1=!memcmp(out,perm,N*4);
    // decode Fenwick
    t0=now();for(int r=0;r<reps;r++){memset(t,0,(N+1)*4);for(int i=0;i<N;i++)fw_add(t,N,i,1);BW d={buf,0};
      for(int i=0;i<N;i++){uint32_t idx=get(&d,bits(N-i));int v=fw_find(t,N,idx);out[i]=v;fw_add(t,N,v,-1);}}
    double df=(now()-t0)/reps*1e3;int ok2=!memcmp(out,perm,N*4);
    // raw: read N values of ceil(log2 N) bits
    int vb=bits(N);uint8_t*raw=calloc((size_t)N*vb/8+8,1);BW rw={raw,0};for(int i=0;i<N;i++)put(&rw,perm[i],vb);
    t0=now();for(int r=0;r<reps;r++){BW d={raw,0};for(int i=0;i<N;i++)out[i]=get(&d,vb);}double dr=(now()-t0)/reps*1e3;
    printf("%-7d %-12zu %-10zu %-10.4f %-16.4f %-18.4f %-10.4f %s\n",N,hb,(size_t)N*vb/8,enc,dn,df,dr,(ok1&&ok2)?"exact":"MISMATCH");
    free(perm);free(out);free(buf);free(t);free(lst);free(raw);}
  return 0;}
