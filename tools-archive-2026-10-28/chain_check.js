const fs=require('fs');
eval(fs.readFileSync('lab_run.js','utf8').split('const fs=require')[0]);
function makeExample(){ let seed=20261024; const rnd=()=> (seed=(Math.imul(seed,1664525)+1013904223)>>>0)/4294967296;
 const gates=['gate-1','gate-2','gate-3','pool','clubhouse','east-ped'], res=['granted','granted','granted','denied','held-open']; let s='',t=18*3600;
 while(s.length<140000){ t+=1+Math.floor(rnd()*90); const hh=String(Math.floor(t/3600)%24).padStart(2,'0'),mm=String(Math.floor(t/60)%60).padStart(2,'0'),ss=String(t%60).padStart(2,'0');
  const g=gates[Math.floor(rnd()*gates.length)], r=res[Math.floor(rnd()*res.length)]; s+=`2026-10-24 ${hh}:${mm}:${ss} INFO ${g} badge=${1000+Math.floor(rnd()*400)} result=${r}\n`; }
 return Buffer.from(s); }
function pack(b){ const o=Buffer.alloc(Math.ceil(b.length/8)); for(let i=0;i<b.length;i++) if(b[i]) o[i>>3]|=128>>(i&7); return o; }
const chains={
 gate:{data:makeExample().subarray(0,16384), list:{
  'Sort8 top32 > Juggle 0+1 > Sort8 top64':[{t:'B',m:8,k:32,dir:1},{t:'J',L:1,vals:[0,1]},{t:'B',m:8,k:64,dir:1}],
  'Juggle 01011011 > Sort8 top16 > HaBiTS map6':[{t:'J',L:8,vals:[0b01011011]},{t:'B',m:8,k:16,dir:1},{t:'H',cin:0,p:5}],
  'Sort4 top8 > HaBiTS map6':[{t:'B',m:4,k:8,dir:1},{t:'H',cin:0,p:5}]}},
 text:{data:fs.readFileSync('/usr/lib/python3.12/pydoc_data/topics.py').subarray(0,16384), list:{
  'Sort8 top64 > Juggle 01000001 > Sort8 top32':[{t:'B',m:8,k:64,dir:1},{t:'J',L:8,vals:[0b01000001]},{t:'B',m:8,k:32,dir:1}],
  'HaBiTS flip map2':[{t:'H',cin:1,p:1}]}}};
fs.mkdirSync('cc',{recursive:true});
for(const [name,{data,list}] of Object.entries(chains)){
  fs.writeFileSync(`cc/${name}__orig.bin`, data);
  const orig=bytesToBits(data); let i=0;
  for(const [label,steps] of Object.entries(list)){
    let b=orig, hdr=0, recs=[]; for(const v of steps){ const r=applyVariant(b,v); b=r.out; hdr+=r.hdr; recs.push(r.rec); }
    let x=b; for(let j=recs.length-1;j>=0;j--) x=invertRec(x,recs[j]);
    fs.writeFileSync(`cc/${name}__${i}.bin`, pack(b)); fs.writeFileSync(`cc/${name}__${i}.meta`, JSON.stringify({label,hdr:hdr+32,ok:eqBits(x,orig),ctx:Math.round((costContext(b,12)+hdr+32)/orig.length*1e6)})); i++;
  }
}
