import json,base64,re,html,subprocess,sys
d=json.load(open(sys.argv[1]))['data'];s=base64.b64decode(d['bytes_b64']).decode()
def conv(m):
    tex=html.unescape(m.group(1))
    return '<div class="eq">'+subprocess.run(['pandoc','-f','markdown','-t','html','--mathml'],input='$$'+tex+'$$',capture_output=True,text=True).stdout+'</div>'
s=re.sub(r'<pre><code class="language-latex">(.*?)</code></pre>',conv,s,flags=re.S)
css='''<style>body{font-family:"Times New Roman",Georgia,serif;font-size:11pt;line-height:1.45;max-width:none}
h1{font-size:20pt;text-align:center;line-height:1.25;margin-bottom:0.3em}h2{font-size:14pt;margin-top:1.4em}h3{font-size:12pt}
table{border-collapse:collapse;width:100%;font-size:9.5pt;margin:0.8em 0;page-break-inside:avoid}th,td{border:1px solid #999;padding:3px 6px;vertical-align:top}th{background:#eee}
.eq{margin:0.8em 0;text-align:center}.eq p{margin:0}</style></head>'''
s=s.replace('</head>',css,1)
s=re.sub(r' · <span data-atom="mention"[^<]*</span>','',s)
s=re.sub(r'<time data-atom="date" datetime="[^"]*">2026-10-29</time>','29 October 2026',s)
open('paper2.html','w').write(s)
