#include <orc/OrcFile.hh>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
using namespace std;
static vector<unsigned char> rd(const char*f){FILE*p=fopen(f,"rb");vector<unsigned char>v;unsigned char b[65536];size_t n;while((n=fread(b,1,sizeof b,p))>0)v.insert(v.end(),b,b+n);fclose(p);return v;}
int main(int ac,char**av){
  auto d=rd(av[1]); int R=atoi(av[2]); string out=av[3]; string codec=av[4];
  size_t rows=d.size()/R;
  string sch="struct<"; for(int i=0;i<R;i++){ if(i)sch+=","; sch+="c"+to_string(i)+":tinyint"; } sch+=">";
  auto type=orc::Type::buildTypeFromString(sch);
  orc::WriterOptions wo; wo.setCompression(codec=="zstd"?orc::CompressionKind_ZSTD:codec=="zlib"?orc::CompressionKind_ZLIB:orc::CompressionKind_SNAPPY);
  auto tw0=chrono::steady_clock::now();
  { auto os=orc::writeLocalFile(out); auto w=orc::createWriter(*type,os.get(),wo);
    size_t B=65536; auto batch=w->createRowBatch(B); auto*st=dynamic_cast<orc::StructVectorBatch*>(batch.get());
    for(size_t r0=0;r0<rows;r0+=B){ size_t n=min(B,rows-r0);
      for(int c=0;c<R;c++){ auto*col=dynamic_cast<orc::LongVectorBatch*>(st->fields[c]); for(size_t i=0;i<n;i++) col->data[i]=(int8_t)d[(r0+i)*R+c]; col->numElements=n; }
      st->numElements=n; w->add(*batch);} w->close(); }
  double wms=chrono::duration<double,milli>(chrono::steady_clock::now()-tw0).count();
  vector<double> ts; vector<unsigned char> back(rows*R);
  for(int rep=0;rep<9;rep++){
    auto t0=chrono::steady_clock::now();
    orc::ReaderOptions ro; auto rdr=orc::createReader(orc::readLocalFile(out),ro); orc::RowReaderOptions rro; auto rr=rdr->createRowReader(rro);
    auto batch=rr->createRowBatch(65536); size_t pos=0;
    while(rr->next(*batch)){ auto*st=dynamic_cast<orc::StructVectorBatch*>(batch.get());
      for(int c=0;c<R;c++){auto*col=dynamic_cast<orc::LongVectorBatch*>(st->fields[c]); for(size_t i=0;i<batch->numElements;i++) back[(pos+i)*R+c]=(unsigned char)col->data[i];}
      pos+=batch->numElements;}
    ts.push_back(chrono::duration<double,milli>(chrono::steady_clock::now()-t0).count());
  }
  if(memcmp(back.data(),d.data(),rows*R)){puts("BAD");return 1;}
  sort(ts.begin(),ts.end()); FILE*f=fopen(out.c_str(),"rb");fseek(f,0,2);long sz=ftell(f);fclose(f);
  printf("%zu %ld %.2f %.2f\n",d.size(),sz+(long)(d.size()-rows*R),ts[4],wms);
}
