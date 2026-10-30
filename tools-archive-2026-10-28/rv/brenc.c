#include <brotli/encode.h>
#include <stdio.h>
#include <stdlib.h>
int main(int c,char**v){ // in out q mode lgwin lgblock dlcm
 FILE*f=fopen(v[1],"rb");fseek(f,0,2);size_t n=ftell(f);rewind(f);unsigned char*b=malloc(n);fread(b,1,n,f);fclose(f);
 BrotliEncoderState*s=BrotliEncoderCreateInstance(0,0,0);
 BrotliEncoderSetParameter(s,BROTLI_PARAM_QUALITY,atoi(v[3]));BrotliEncoderSetParameter(s,BROTLI_PARAM_MODE,atoi(v[4]));
 BrotliEncoderSetParameter(s,BROTLI_PARAM_LGWIN,atoi(v[5]));if(atoi(v[6]))BrotliEncoderSetParameter(s,BROTLI_PARAM_LGBLOCK,atoi(v[6]));
 BrotliEncoderSetParameter(s,BROTLI_PARAM_DISABLE_LITERAL_CONTEXT_MODELING,atoi(v[7]));BrotliEncoderSetParameter(s,BROTLI_PARAM_SIZE_HINT,n);
 size_t on=BrotliEncoderMaxCompressedSize(n)+1024;unsigned char*o=malloc(on);const unsigned char*ni=b;size_t ai=n;unsigned char*no=o;size_t ao=on;
 BrotliEncoderCompressStream(s,BROTLI_OPERATION_FINISH,&ai,&ni,&ao,&no,0);FILE*g=fopen(v[2],"wb");fwrite(o,1,on-ao,g);fclose(g);return 0;}
