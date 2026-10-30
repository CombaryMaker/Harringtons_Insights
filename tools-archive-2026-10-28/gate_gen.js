let seed=20261024; const rnd=()=> (seed=(Math.imul(seed,1664525)+1013904223)>>>0)/4294967296;
const gates=['gate-1','gate-2','gate-3','pool','clubhouse','east-ped'], res=['granted','granted','granted','denied','held-open']; let s='',t=18*3600;
while(s.length<140000){ t+=1+Math.floor(rnd()*90); const hh=String(Math.floor(t/3600)%24).padStart(2,'0'),mm=String(Math.floor(t/60)%60).padStart(2,'0'),ss=String(t%60).padStart(2,'0');
 const g=gates[Math.floor(rnd()*gates.length)], r=res[Math.floor(rnd()*res.length)]; s+=`2026-10-24 ${hh}:${mm}:${ss} INFO ${g} badge=${1000+Math.floor(rnd()*400)} result=${r}\n`; }
process.stdout.write(Buffer.from(s));
