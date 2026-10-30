import os,re,subprocess,html
B='/mnt/user-data/outputs/book2'
files=sorted(f for f in os.listdir(B) if f.endswith('.md') and f!='00-Outline.md')
order=['00-Introduction.md']+[f for f in files if re.match(r'\d\d-',f) and f!='00-Introduction.md']+[f for f in files if re.match(r'[A-E]-',f)]
md='\n\n'.join(open(f'{B}/{f}').read().strip() for f in order)
md=re.sub(r'(?m)^((?![-*\d|>#\s]).+|\*\*[^\n]*)\n(?=(- |\d+\. ))',r'\1\n\n',md)
open('all.md','w').write(md)
subprocess.run(['pandoc','all.md','-f','markdown-tex_math_dollars-subscript-superscript','-t','html','-o','body.html'],check=True)
parts={1:'Part I — Framework',4:'Part II — Strongest Results on Standard Formats',9:'Part III — The Harrington Compression Method',14:'Part IV — Further Methods and Data Types',19:'Part V — Cost and Economic Potential',21:'Part VI — Limits, Lessons and Licensing'}
toc=['<h1>Contents</h1>','<div style="margin-left:1.2em">Introduction</div>']
for f in order[1:]:
    t=open(f'{B}/{f}').readline().lstrip('# ').strip()
    m=re.match(r'Chapter (\d+)',t)
    if m and int(m.group(1)) in parts: toc.append(f'<h3 style="margin:0.9em 0 0.2em">{parts[int(m.group(1))]}</h3>')
    if f.startswith('A-'): toc.append('<h3 style="margin:0.9em 0 0.2em">Appendices</h3>')
    toc.append(f'<div style="margin-left:1.2em">{html.escape(t)}</div>')
css=open('style.css').read()
head=f'<!doctype html><html><head><meta charset="utf-8"><title>Harrington Insights</title><style>{css}</style></head><body><h1 class="title">Harrington Insights</h1><p class="subtitle">Lossless Compression Research: HCM, Combary Transforms, and No-Pain Optimisation of Standard Formats</p><p class="author">Michael Harrington<br>Second edition — October 2026</p>'
open('book.html','w').write(head+'\n'.join(toc)+open('body.html').read()+'</body></html>')
print(len(order),'files')
