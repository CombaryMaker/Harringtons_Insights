# EPUB prep: wide tables (5+ columns) -> PNG images (tap-to-zoom on Kindle); narrow tables stay as text.
import re,subprocess,html,json
P='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/'
md=open(P+'book2pdf/all.md').read()
subprocess.run(['pandoc','-f','markdown-tex_math_dollars-subscript-superscript','-t','html','-o','body.html'],input=md.encode(),check=True)
b=open('body.html').read(); tables=[]
def rep(m):
    t=m.group(0); hdr=re.search(r'<thead>.*?</thead>',t,re.S); cols=len(re.findall(r'<th',hdr.group(0))) if hdr else len(re.findall(r'<td',re.search(r'<tr.*?</tr>',t,re.S).group(0)))
    if cols<5: return t
    i=len(tables); tables.append(t); return f'<div class="tbl"><img src="img/table{i:03d}.png" alt="Table {i+1}"/><p class="tnote">Tap the table to enlarge.</p></div>'
b=re.sub(r'<table.*?</table>',rep,b,flags=re.S)
open('body_img.html','w').write(b)
css="body{font-family:'Liberation Serif',serif;font-size:15px;margin:0;padding:12px;background:#fff}table{border-collapse:collapse}th,td{border:1px solid #888;padding:4px 7px;vertical-align:top}th{background:#eee}"
open('tables.html','w').write('<html><head><meta charset="utf-8"><style>'+css+'</style></head><body>'+''.join(f'<div id="t{i}" style="display:inline-block;padding:8px;background:#fff">{t}</div><br>' for i,t in enumerate(tables))+'</body></html>')
print(len(tables),'wide tables')
