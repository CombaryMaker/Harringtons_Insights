#include "lzfse.h"
#include <emmintrin.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
using namespace std;
static void* ES; static void* DS;
static void comp(const char*p,size_t n,string*o){o->resize(n+n/2+1024);size_t r=lzfse_encode_buffer((uint8_t*)&(*o)[0],o->size(),(const uint8_t*)p,n,ES);o->resize(r);}
static void dec(const string&c,unsigned char*o,size_t n){lzfse_decode_buffer(o,n,(const uint8_t*)c.data(),c.size(),DS);}
static string rd(const char*f){FILE*p=fopen(f,"rb");string s;char b[1<<16];size_t n;while((n=fread(b,1,sizeof b,p))>0)s.append(b,n);fclose(p);return s;}
static double now(){return chrono::duration<double,micro>(chrono::steady_clock::now().time_since_epoch()).count();}
static void undelta(unsigned char*o,const unsigned char*t,size_t U,size_t R){
  size_t st=R<16?R:16; memcpy(o,t,min(R,U)); size_t i=R;
  for(;i+16<=U;i+=st){__m128i a=_mm_loadu_si128((const __m128i*)(t+i)),b=_mm_loadu_si128((const __m128i*)(o+i-R));_mm_storeu_si128((__m128i*)(o+i),_mm_add_epi8(a,b));}
  for(;i<U;i++)o[i]=t[i]+o[i-R];}
int main(int ac,char**av){ES=malloc(lzfse_encode_scratch_size());DS=malloc(lzfse_decode_scratch_size());
  string d=rd(av[1]); size_t R=atoi(av[2]),U=d.size(); int N=atoi(av[3]);
  // plain
  string cp; comp(d.data(),U,&cp);
  // delta
  string dl(U,0); for(size_t i=0;i<U;i++) dl[i]=(char)(d[i]-(i>=R?d[i-R]:0));
  string cd; comp(dl.data(),U,&cd);
  // activity
  string fl((U+7)/8,0),li; for(size_t i=0;i<U;i++){ if(!(i>=R&&d[i]==d[i-R])){fl[i>>3]|=0x80>>(i&7);li.push_back(d[i]);} }
  string cf,cl; comp(fl.data(),fl.size(),&cf); comp(li.data(),li.size(),&cl);
  vector<unsigned char> out(U),tmp(U),fb(fl.size()),lb(li.size()+1);
  vector<double> tp,td,ta;
  for(int r=0;r<N;r++){
    double t=now(); dec(cp,out.data(),U); tp.push_back(now()-t);
    if(r==0&&memcmp(out.data(),d.data(),U)) {puts("BAD plain");return 1;}
    t=now(); dec(cd,tmp.data(),U); undelta(out.data(),tmp.data(),U,R); td.push_back(now()-t);
    if(r==0&&memcmp(out.data(),d.data(),U)) {puts("BAD delta");return 1;}
    t=now(); dec(cf,fb.data(),fb.size()); dec(cl,lb.data(),lb.size());
    size_t j=0; for(size_t i=0;i<U;i++){ if((fb[i>>3]>>(7-(i&7)))&1) out[i]=lb[j++]; else out[i]=out[i-R]; } ta.push_back(now()-t);
    if(r==0&&memcmp(out.data(),d.data(),U)) {puts("BAD act");return 1;}
  }
  auto med=[](vector<double>v){sort(v.begin(),v.end());return v[v.size()/2];};
  printf("%zu %zu %.1f %zu %.1f %zu %.1f\n",U,cp.size(),med(tp),cd.size()+2,med(td),cf.size()+cl.size()+6,med(ta));
}
