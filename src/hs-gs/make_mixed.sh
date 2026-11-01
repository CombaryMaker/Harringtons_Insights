# Builds mixed.pdf used in the tests (needs qpdf and the pdf.js test PDFs: github.com/mozilla/pdf.js, test/pdfs):
# the 352-page book freeculture.pdf followed by 19 image-heavy single-page PDFs, repeated 3 times (57 pages).
cd pdfjs/test/pdfs
H="bug1721218_reduced.pdf issue5567.pdf 22060_A1_01_Plans.pdf issue1905.pdf perf_codecs_jpeg_cmyk.pdf images.pdf issue5549.pdf perf_structure_colorspace_images.pdf perf_images_smask.pdf perf_images_masks.pdf issue12841_reduced.pdf perf_images_cmyk.pdf issue13520.pdf perf_images_flate_predictor.pdf perf_images_jpeg_large.pdf perf_codecs_jpeg_gray_progressive.pdf perf_images_jpeg_thumbnails.pdf perf_images_16bit.pdf issue20513.pdf"
args=""; for r in 1 2 3; do for h in $H; do args="$args $h 1"; done; done
qpdf --empty --pages freeculture.pdf 1-z $args -- ../../../mixed.pdf
