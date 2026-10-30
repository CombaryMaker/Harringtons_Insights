#include <charls/charls.hpp>
#include <cstdio>
#include <vector>
#include <chrono>
#include <algorithm>
#include <cstring>
using namespace std;
static double now(){return chrono::duration<double,milli>(chrono::steady_clock::now().time_since_epoch()).count();}
int main(int ac,char**av){ // pgm16 out tuned(0/1)
  FILE*f=fopen(av[1],"rb"); int w,h,mv; fscanf(f,"P5 %d %d %d",&w,&h,&mv); fgetc(f);
  vector<unsigned char> raw(w*h*2); fread(raw.data(),1,raw.size(),f); fclose(f);
  vector<uint16_t> px(w*h); for(int i=0;i<w*h;i++) px[i]=raw[2*i]<<8|raw[2*i+1];
  int bits=0; while((1<<bits)<=mv) bits++;
  bool tuned=atoi(av[3]); vector<double> te; vector<unsigned char> enc;
  for(int r=0;r<3;r++){ double t=now();
    charls::jpegls_encoder e; e.frame_info({(uint32_t)w,(uint32_t)h,bits,1});
    if(tuned){ charls::jpegls_pc_parameters p{}; p.maximum_sample_value=mv; p.threshold1=12; p.threshold2=32; p.threshold3=64; p.reset_value=255; e.preset_coding_parameters(p);}
    enc.resize(e.estimated_destination_size()); e.destination(enc); size_t n=e.encode(px); enc.resize(n); te.push_back(now()-t);}
  vector<double> td; vector<uint16_t> out;
  for(int r=0;r<9;r++){ double t=now(); charls::jpegls_decoder d(enc,true); out.resize(w*h); d.decode(out); td.push_back(now()-t);}
  bool ok=!memcmp(out.data(),px.data(),px.size()*2);
  sort(te.begin(),te.end()); sort(td.begin(),td.end());
  FILE*g=fopen(av[2],"wb"); fwrite(enc.data(),1,enc.size(),g); fclose(g);
  printf("{\"size\":%zu,\"enc_ms\":%.2f,\"dec_ms\":%.2f,\"ok\":%s}\n",enc.size(),te[1],td[4],ok?"true":"false");}
