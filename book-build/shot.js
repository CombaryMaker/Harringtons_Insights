const {chromium}=require('playwright');(async()=>{const b=await chromium.launch();const p=await b.newPage({viewport:{width:1100,height:800},deviceScaleFactor:2});
await p.goto('file://'+process.cwd()+'/tables.html');const n=await p.$$eval('div[id^=t]',d=>d.length);
for(let i=0;i<n;i++){const e=await p.$('#t'+i);await e.screenshot({path:'img/table'+String(i).padStart(3,'0')+'.png'});}await b.close();})();
