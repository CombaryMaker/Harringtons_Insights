const {QuadExchangeConvert}=require('./qx.js');const fs=require('fs');const cp=require('child_process');
const C=process.argv[2],Z=process.argv[3];let T={zp:0,zq:0,lp:0,lq:0,gp:0,gq:0};
for(const f of fs.readdirSync(C).sort()){const b=fs.readFileSync(C+'/'+f);
 const q=Buffer.from(QuadExchangeConvert(new Uint8Array(b)).outBytes);fs.writeFileSync('q.bin',q);
 const sz=(cmd,file)=>cp.execSync(`${cmd} < ${file} | wc -c`).toString()*1;
 const r=[sz(Z+'/zstd/programs/zstd -q --ultra -22 -c',C+'/'+f),sz(Z+'/zstd/programs/zstd -q --ultra -22 -c','q.bin')+5,
 sz(Z+'/lz4/lz4 -q -12 -c',C+'/'+f),sz(Z+'/lz4/lz4 -q -12 -c','q.bin')+5,sz('gzip -9nc',C+'/'+f),sz('gzip -9nc','q.bin')+5];
 ['zp','zq','lp','lq','gp','gq'].forEach((k,i)=>T[k]+=r[i]);console.log(f,r.join(' '));}
console.log('TOTAL',JSON.stringify(T));
