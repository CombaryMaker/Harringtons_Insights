# Generates "scanned" page images with very different OCR cost: dense small-print pages, normal pages and nearly
# blank pages, with the dense ones clustered at the end (the hard case for equal splitting). MIT License.
import random, textwrap
from PIL import Image, ImageDraw, ImageFont
random.seed(4)
dickens = open('/tmp/sil/dickens', encoding='latin-1').read().split()
def page(i, kind):
    img = Image.new('L', (1700, 2200), 250); d = ImageDraw.Draw(img); r = random.Random(i)
    size, lines, width = {'blank': (26, 3, 70), 'normal': (26, 45, 75), 'dense': (18, 95, 130)}[kind]
    f = ImageFont.load_default(size=size); w0 = (i * 900) % (len(dickens) - 2000)
    y = 80
    for ln in textwrap.wrap(' '.join(dickens[w0:w0 + 1800]), width)[:lines]:
        d.text((90 + r.randint(-2, 2), y), ln, fill=r.randint(10, 60), font=f); y += size + 4
    for _ in range(4000): img.putpixel((r.randrange(1700), r.randrange(2200)), r.randint(140, 255))
    img.save(f'pages/p{i:04d}.png')
kinds = ['normal'] * 100 + ['blank'] * 40 + ['dense'] * 40
for i, k in enumerate(kinds, 1): page(i, k)
open('pages.txt', 'w').write('\n'.join(f'/tmp/ocr/pages/p{i:04d}.png' for i in range(1, len(kinds) + 1)) + '\n')
print(len(kinds))
