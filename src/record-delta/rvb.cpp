// in-process bench: plain / record delta / activity map with zstd, gzip(zlib), snappy
#include <zstd.h>
#include <zlib.h>
#include <lzma.h>
#include <snappy.h>
#include <emmintrin.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
using namespace std; typedef vector<unsigned char> B;
static double now(){return chrono::duration<double,micro>(chrono::steady_clock::now().time_since_epoch()).count();}
static B rd(const char*f){FILE*p=fopen(f,"rb");B s;unsigned char b[1<<16];size_t n;while((n=fread(b,1,sizeof b,p))>0)s.insert(s.end(),b,b+n);fclose(p);return s;}
static void undelta_old(unsigned char*o,const unsigned char*t,size_t U,size_t R);
static void undelta_p2(unsigned char*o,const unsigned char*t,size_t U,size_t R){ // R in {1,2,4,8}: in-register prefix sum
  __m128i carry=_mm_setzero_si128(); size_t i=0;
  for(;i+16<=U;i+=16){ __m128i x=_mm_loadu_si128((const __m128i*)(t+i));
    if(R<=1) x=_mm_add_epi8(x,_mm_slli_si128(x,1));
    if(R<=2) x=_mm_add_epi8(x,_mm_slli_si128(x,2));
    if(R<=4) x=_mm_add_epi8(x,_mm_slli_si128(x,4));
    x=_mm_add_epi8(x,_mm_slli_si128(x,8));
    x=_mm_add_epi8(x,carry); _mm_storeu_si128((__m128i*)(o+i),x);
    // carry = last R bytes broadcast
    if(R==1) carry=_mm_set1_epi8(o[i+15]);
    else if(R==2) carry=_mm_set1_epi16(*(short*)(o+i+14));
    else if(R==4) carry=_mm_set1_epi32(*(int*)(o+i+12));
    else carry=_mm_set1_epi64x(*(long long*)(o+i+8)); }
  for(;i<U;i++) o[i]=t[i]+(i>=R?o[i-R]:0);}
static void undelta(unsigned char*o,const unsigned char*t,size_t U,size_t R){ if(getenv("FASTU")&&(R==1||R==2||R==4||R==8)) undelta_p2(o,t,U,R); else undelta_old(o,t,U,R);}
static void undelta_old(unsigned char*o,const unsigned char*t,size_t U,size_t R){
  size_t st=R<16?R:16; memcpy(o,t,min(R,U)); size_t i=R;
  for(;i+16<=U;i+=st){__m128i a=_mm_loadu_si128((const __m128i*)(t+i)),b=_mm_loadu_si128((const __m128i*)(o+i-R));_mm_storeu_si128((__m128i*)(o+i),_mm_add_epi8(a,b));}
  for(;i<U;i++)o[i]=t[i]+o[i-R];}
string C; int L;
static B comp(const B&in){ B o;
  if(C=="zstd"){o.resize(ZSTD_compressBound(in.size())); o.resize(ZSTD_compress(o.data(),o.size(),in.data(),in.size(),L));}
  else if(C=="xz"){o.resize(lzma_stream_buffer_bound(in.size())); size_t p=0; lzma_easy_buffer_encode(L,LZMA_CHECK_CRC64,NULL,in.data(),in.size(),o.data(),&p,o.size()); o.resize(p);}
  else if(C=="gzip"){z_stream s{}; deflateInit2(&s,L,Z_DEFLATED,31,9,Z_DEFAULT_STRATEGY); o.resize(deflateBound(&s,in.size())+32); s.next_in=(Bytef*)in.data(); s.avail_in=in.size(); s.next_out=o.data(); s.avail_out=o.size(); deflate(&s,Z_FINISH); o.resize(s.total_out); deflateEnd(&s);}
  else {string t; snappy::Compress((const char*)in.data(),in.size(),&t); o.assign(t.begin(),t.end());}
  return o;}
static void dec(const B&in,unsigned char*out,size_t U){
  if(C=="zstd") ZSTD_decompress(out,U,in.data(),in.size());
  else if(C=="xz"){uint64_t ml=UINT64_MAX; size_t ip=0,op=0; lzma_stream_buffer_decode(&ml,0,NULL,in.data(),&ip,in.size(),out,&op,U);}
  else if(C=="gzip"){z_stream s{}; inflateInit2(&s,31); s.next_in=(Bytef*)in.data(); s.avail_in=in.size(); s.next_out=out; s.avail_out=U; inflate(&s,Z_FINISH); inflateEnd(&s);}
  else snappy::RawUncompress((const char*)in.data(),in.size(),(char*)out);}
static double med(vector<double>v){sort(v.begin(),v.end());return v[v.size()/2];}
int main(int ac,char**av){
  C=av[1]; L=atoi(av[2]); B d=rd(av[3]); size_t R=atoi(av[4]),U=d.size(); int N=atoi(av[5]);
  vector<double> ep,ed,ea,dp,dd,da; B cp,cd,cf,cl; size_t ln=0;
  for(int r=0;r<3;r++){
    double t=now(); cp=comp(d); ep.push_back(now()-t);
    t=now(); B dl(U); for(size_t i=0;i<U;i++) dl[i]=d[i]-(i>=R?d[i-R]:0); cd=comp(dl); ed.push_back(now()-t);
    t=now(); B fl((U+7)/8,0),li; for(size_t i=0;i<U;i++) if(!(i>=R&&d[i]==d[i-R])){fl[i>>3]|=0x80>>(i&7);li.push_back(d[i]);} cf=comp(fl); cl=comp(li); ln=li.size(); ea.push_back(now()-t);
  }
  B out(U),tmp(U),fb((U+7)/8),lb(ln+1);
  for(int r=0;r<N;r++){
    double t=now(); dec(cp,out.data(),U); dp.push_back(now()-t); if(r==0&&memcmp(out.data(),d.data(),U)){puts("BAD plain");return 1;}
    t=now(); dec(cd,tmp.data(),U); undelta(out.data(),tmp.data(),U,R); dd.push_back(now()-t); if(r==0&&memcmp(out.data(),d.data(),U)){puts("BAD delta");return 1;}
    t=now(); dec(cf,fb.data(),fb.size()); dec(cl,lb.data(),ln); size_t j=0; for(size_t i=0;i<U;i++){ if((fb[i>>3]>>(7-(i&7)))&1) out[i]=lb[j++]; else out[i]=out[i-R]; } da.push_back(now()-t);
    if(r==0&&memcmp(out.data(),d.data(),U)){puts("BAD act");return 1;}
  }
  printf("{\"U\":%zu,\"plain\":%zu,\"enc_plain_us\":%.1f,\"dec_plain_us\":%.1f,\"delta\":%zu,\"enc_delta_us\":%.1f,\"dec_delta_us\":%.1f,\"act\":%zu,\"enc_act_us\":%.1f,\"dec_act_us\":%.1f,\"ok\":true}\n",
    U,cp.size(),med(ep),med(dp),cd.size()+2,med(ed),med(dd),cf.size()+cl.size()+6,med(ea),med(da));
}
