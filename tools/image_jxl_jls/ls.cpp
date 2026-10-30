// JPEG-LS (CharLS) bench: read 16-bit PGM, encode with optional preset T1,T2,T3,RESET, decode N times, verify.
#include <charls/charls.hpp>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <string>
using namespace std;
static vector<uint16_t> readpgm(const char*fn,int&w,int&h,int&mx){FILE*f=fopen(fn,"rb");char m[3];if(fscanf(f,"%2s %d %d %d",m,&w,&h,&mx)!=4)exit(1);fgetc(f);vector<uint16_t>p((size_t)w*h);vector<uint8_t>b(p.size()*2);if(fread(b.data(),1,b.size(),f)!=b.size())exit(2);fclose(f);for(size_t i=0;i<p.size();i++)p[i]=(b[2*i]<<8)|b[2*i+1];return p;}
int main(int argc,char**argv){
  int w,h,mx; auto px=readpgm(argv[1],w,h,mx); int reps=atoi(argv[2]);
  int bits=0; while((1<<bits)<=mx) bits++;
  int t1=argc>3?atoi(argv[3]):0,t2=argc>4?atoi(argv[4]):0,t3=argc>5?atoi(argv[5]):0,rs=argc>6?atoi(argv[6]):0;
  charls::jpegls_encoder enc; enc.frame_info({(uint32_t)w,(uint32_t)h,bits,1});
  if(t1||t2||t3||rs){ charls::jpegls_pc_parameters pc{mx,t1,t2,t3,rs?rs:64}; enc.preset_coding_parameters(pc); }
  vector<uint8_t> out(enc.estimated_destination_size()); enc.destination(out); size_t n=enc.encode(px); out.resize(n);
  vector<double> t; vector<uint16_t> d(px.size());
  for(int r=0;r<reps;r++){ auto s=chrono::steady_clock::now(); charls::jpegls_decoder dec(out,true); dec.decode(d); t.push_back(chrono::duration<double,milli>(chrono::steady_clock::now()-s).count()); }
  sort(t.begin(),t.end()); bool ok=memcmp(d.data(),px.data(),px.size()*2)==0;
  printf("%s bits=%d T=%d,%d,%d,%d size %zu decode %.2f ms %s\n",argv[1],bits,t1,t2,t3,rs,n,t[reps/2],ok?"OK":"FAIL");
}
