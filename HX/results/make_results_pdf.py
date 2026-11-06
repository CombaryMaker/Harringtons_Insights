# make_results_pdf.py - PDF of the All-Software results list plus the two radar maps. (c) 2026 Michael Hugh Harrington.
import sys
from reportlab.lib.pagesizes import letter, landscape
from reportlab.pdfgen import canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib.utils import ImageReader
import glob
fp = (glob.glob('/usr/share/fonts/**/LiberationMono-Regular.ttf', recursive=True) + glob.glob('/usr/share/fonts/**/DejaVuSansMono.ttf', recursive=True))[0]
pdfmetrics.registerFont(TTFont('Mono', fp))
src, out, imgs = sys.argv[1], sys.argv[2], sys.argv[3:]
lines = open(src, encoding='utf-8').read().expandtabs(4).split('\n')
W, H = letter; fs, lead, mx, top = 7.6, 9.6, 36, 40
per = int((H - 2 * top) // lead)
c = canvas.Canvas(out, pagesize=letter, initialFontName='Mono'); c.setTitle('HX - All Software Test Results, 6 Nov 2026'); c.setAuthor('Michael Hugh Harrington')
pages = [lines[i:i + per] for i in range(0, len(lines), per)]; total = len(pages) + len(imgs)
for n, pg in enumerate(pages, 1):
    c.setFont('Mono', fs); y = H - top
    for l in pg: c.drawString(mx, y, l); y -= lead
    c.setFont('Mono', 7); c.drawCentredString(W / 2, 20, f'Page {n} of {total}  -  HX is Patent Pending (U.S. App. 64/192,843)'); c.showPage()
for k, im in enumerate(imgs, len(pages) + 1):
    c.setPageSize(landscape(letter)); LW, LH = landscape(letter); ir = ImageReader(im); iw, ih = ir.getSize()
    s = min((LW - 60) / iw, (LH - 70) / ih); c.drawImage(ir, (LW - iw * s) / 2, 40 + (LH - 70 - ih * s) / 2, iw * s, ih * s)
    c.setFont('Mono', 7); c.drawCentredString(LW / 2, 20, f'Page {k} of {total}  -  HX is Patent Pending (U.S. App. 64/192,843)'); c.showPage()
c.save(); print(total, 'pages')
