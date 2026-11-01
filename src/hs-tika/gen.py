# Generates a clean, valid PDF test set with reportlab (text from Silesia dickens/webster and Canterbury texts;
# "scanned" pages = images of rendered text, so OCR has real work). Michael Hugh Harrington, MIT License.
import os, random, textwrap
from reportlab.lib.pagesizes import letter
from reportlab.pdfgen import canvas
from reportlab.lib.utils import ImageReader
from PIL import Image, ImageDraw, ImageFont
random.seed(1)
W, H = letter
def text_pages(c, text, pages):
    lines = []
    for para in text.split('\n'):
        lines += textwrap.wrap(para, 95) or ['']
    i = 0
    for p in range(pages):
        c.setFont('Times-Roman', 10); y = H - 50
        while y > 50 and i < len(lines):
            c.drawString(50, y, lines[i].encode('latin-1', 'replace').decode('latin-1')); y -= 12; i += 1
        c.showPage()
        if i >= len(lines): i = 0
def scan_image(text, seed):
    img = Image.new('L', (1275, 1650), 250); d = ImageDraw.Draw(img); f = ImageFont.load_default(size=22)
    r = random.Random(seed); y = 60
    for ln in textwrap.wrap(text, 80)[:55]:
        d.text((70 + r.randint(-2, 2), y), ln, fill=r.randint(10, 60), font=f); y += 28
    for _ in range(3000): img.putpixel((r.randrange(1275), r.randrange(1650)), r.randint(120, 255))   # scanner noise
    return img
def scan_pages(c, text, pages, seed0):
    words = text.split()
    for p in range(pages):
        start = (p * 600) % max(1, len(words) - 700)
        img = scan_image(' '.join(words[start:start + 600]), seed0 + p)
        c.drawImage(ImageReader(img), 0, 0, W, H); c.showPage()
dickens = open('/tmp/sil/dickens', encoding='latin-1').read()
webster = open('/tmp/sil/webster', encoding='latin-1').read()
cant = {f: open('/tmp/bench/cant/canterbury/' + f, encoding='latin-1').read() for f in ['alice29.txt', 'asyoulik.txt', 'lcet10.txt', 'plrabn12.txt']}
os.makedirs('set', exist_ok=True)
c = canvas.Canvas('clean_text_1500.pdf'); c.setTitle('clean text'); text_pages(c, dickens, 1500); c.save()
c = canvas.Canvas('clean_mixed.pdf'); c.setTitle('clean mixed'); text_pages(c, dickens, 400); scan_pages(c, webster, 40, 100); c.save()
for k, (f, t) in enumerate(cant.items()):
    for n, sc in [(20, 0), (60, 4), (120, 8)]:
        c = canvas.Canvas(f'set/{f[:-4]}_{n}p_{sc}scan.pdf'); text_pages(c, t, n); scan_pages(c, t, sc, k * 1000 + n); c.save()
for k in range(8):
    c = canvas.Canvas(f'set/webster_part{k}.pdf'); text_pages(c, webster[k * 500000:(k + 1) * 500000], 50 + 10 * k); scan_pages(c, webster[k * 500000:], k % 3, 5000 + k); c.save()
print('done')
