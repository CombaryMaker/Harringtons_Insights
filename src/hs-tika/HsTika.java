// HsTika.java - Harrington Sets wrapper for Apache Tika: extracts text from ONE large PDF on every core.
// Original method by Michael Hugh Harrington, 2026. MIT License (full text at end of file).
//
// Tika's own parallel mode (Tika Pipes) spreads separate documents across workers; a single large PDF is parsed
// on one thread. HsTika splits the PDF into contiguous page sets (contiguous pages share fonts/resources), hands
// them out from a shared queue with sets shrinking toward the end (guided scheduling, as in hs_gs), parses each
// set with Tika's normal AutoDetectParser on its own thread inside one JVM, and joins the text in page order.
//
// Usage:  java -cp tika-app.jar:hs_tika.jar HsTika INPUT.pdf OUTPUT.txt [--jobs N] [--split equal|guided] [--plain]
//         (Windows: use ; instead of : in the class path)
//   --plain  parse the whole PDF with Tika in one piece (the normal single-thread way) - for comparison
// Output is Tika's body text (BodyContentHandler, no length limit), the same handler for both modes.
// Build:  javac -cp tika-app.jar HsTika.java && jar cf hs_tika.jar HsTika*.class
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;
import org.apache.pdfbox.Loader;
import org.apache.pdfbox.pdmodel.PDDocument;
import org.apache.tika.metadata.Metadata;
import org.apache.tika.parser.AutoDetectParser;
import org.apache.tika.parser.ParseContext;
import org.apache.tika.sax.BodyContentHandler;

public class HsTika {
    static String parse(AutoDetectParser p, byte[] pdf) throws Exception {
        BodyContentHandler h = new BodyContentHandler(-1);
        try (InputStream in = new ByteArrayInputStream(pdf)) { p.parse(in, h, new Metadata(), new ParseContext()); }
        return h.toString();
    }
    // splits the PDF into page sets, parses them in parallel, returns the joined text; st = pages, sets, busiest, least busy
    public static String extract(byte[] src, AutoDetectParser parser, int jobs, String split, double[] st) throws Exception {
        int pages; int imgPages = 0;
        try (PDDocument d = Loader.loadPDF(src)) { pages = d.getNumberOfPages();
            for (org.apache.pdfbox.pdmodel.PDPage pg : d.getPages()) { try { var r = pg.getResources(); if (r == null) continue;
                for (var n : r.getXObjectNames()) if (r.isImageXObject(n)) { imgPages++; break; } } catch (Exception ignore) { } } }
        // text-only PDFs parse so fast that splitting costs more than it saves: parse them the normal way
        if (imgPages == 0 && !split.equals("force")) { st[0] = pages; st[1] = 1; st[2] = st[3] = 0; return parse(parser, src); }
        jobs = Math.min(jobs, pages);
        List<int[]> sets = new ArrayList<>();
        if (split.equals("equal")) { int per = (pages + jobs - 1) / jobs; for (int s = 1; s <= pages; s += per) sets.add(new int[]{s, Math.min(pages, s + per - 1)}); }
        else { int s = 1; while (s <= pages) { int left = pages - s + 1, sz = Math.max(1, left / (2 * jobs)); sets.add(new int[]{s, s + sz - 1}); s += sz; } }
        String[] out = new String[sets.size()]; AtomicInteger next = new AtomicInteger(); double[] busy = new double[jobs];
        ExecutorService ex = Executors.newFixedThreadPool(jobs); List<Future<?>> fs = new ArrayList<>();
        for (int w = 0; w < jobs; w++) { final int wi = w; fs.add(ex.submit(() -> {
            try (PDDocument doc = Loader.loadPDF(src)) {   // each worker has its own copy (PDFBox is not thread-safe)
                for (int k; (k = next.getAndIncrement()) < sets.size();) {
                    long s0 = System.nanoTime(); int[] r = sets.get(k);
                    ByteArrayOutputStream bos = new ByteArrayOutputStream();
                    if (k == sets.size() - 1) {
                        // last set: keep the whole document structure (outline/bookmarks, forms, attachments) and drop the
                        // earlier pages, so the document-level text Tika writes after the pages appears exactly once, at the end
                        try (PDDocument full = Loader.loadPDF(src)) { for (int p = 1; p < r[0]; p++) full.removePage(0); full.save(bos); }
                    } else {
                        try (PDDocument sub = new PDDocument()) { for (int p = r[0]; p <= r[1]; p++) sub.importPage(doc.getPage(p - 1)); sub.save(bos); }
                    }
                    out[k] = parse(parser, bos.toByteArray());
                    busy[wi] += (System.nanoTime() - s0) / 1e9;
                }
            } catch (Exception e) { throw new RuntimeException(e); }
            return null; })); }
        try { for (Future<?> f : fs) f.get(); }
        catch (ExecutionException e) {   // a page set could not be split out (damaged or unusual PDF): fall back to one plain parse
            ex.shutdownNow(); st[0] = pages; st[1] = 1; st[2] = st[3] = 0; return parse(parser, src);
        }
        ex.shutdown();
        StringBuilder sb = new StringBuilder(); for (String s : out) sb.append(s);
        double mx = 0, mn = 1e18; for (double b : busy) { mx = Math.max(mx, b); mn = Math.min(mn, b); }
        st[0] = pages; st[1] = sets.size(); st[2] = mx; st[3] = mn;
        return sb.toString();
    }
    public static void main(String[] a) throws Exception {
        if (a.length >= 4 && a[0].equals("--checkdir")) {   // compare plain vs split text for every PDF in a folder
            AutoDetectParser p = new AutoDetectParser(); int minp = Integer.parseInt(a[2]), j = Integer.parseInt(a[3]); int same = 0, diff = 0, err = 0;
            File[] fl = new File(a[1]).listFiles((d, n) -> n.toLowerCase().endsWith(".pdf")); Arrays.sort(fl);
            for (File f : fl) { try { byte[] b = Files.readAllBytes(f.toPath()); int pg; try (PDDocument d = Loader.loadPDF(b)) { pg = d.getNumberOfPages(); }
                    if (pg < minp) continue; String x = parse(p, b), y = extract(b, p, j, "guided", new double[4]);
                    if (x.equals(y)) same++; else { diff++; System.out.println("DIFF " + f.getName() + " pages=" + pg + " plain=" + x.length() + " split=" + y.length()); }
                } catch (Throwable e) { err++; System.out.println("ERR " + f.getName() + " " + e.getClass().getSimpleName()); } }
            System.out.println("checked: identical " + same + ", different " + diff + ", errors " + err); return;
        }
        if (a.length < 2) { System.err.println("usage: HsTika INPUT.pdf OUTPUT.txt [--jobs N] [--split equal|guided] [--plain]"); System.exit(2); }
        int jobs = Runtime.getRuntime().availableProcessors(); String split = "guided"; boolean plain = false;
        for (int i = 2; i < a.length; i++) {
            if (a[i].equals("--jobs")) jobs = Math.max(1, Integer.parseInt(a[++i]));
            else if (a[i].equals("--split")) split = a[++i]; else if (a[i].equals("--plain")) plain = true;
        }
        long t0 = System.nanoTime();
        byte[] src = Files.readAllBytes(Paths.get(a[0]));
        AutoDetectParser parser = new AutoDetectParser();
        if (plain) {
            String txt = parse(parser, src);
            Files.write(Paths.get(a[1]), txt.getBytes(StandardCharsets.UTF_8));
            System.err.printf("plain Tika: %.1f s%n", (System.nanoTime() - t0) / 1e9); return;
        }
        double[] st = new double[4];
        String txt = extract(src, parser, jobs, split, st);
        Files.write(Paths.get(a[1]), txt.getBytes(StandardCharsets.UTF_8));
        System.err.printf("HsTika: %d pages, %d page sets (%s), %d jobs; %.1f s; busiest thread %.1f s, least busy %.1f s%n", (int) st[0], (int) st[1], split, (int) Math.min(jobs, st[0]), (System.nanoTime() - t0) / 1e9, st[2], st[3]);
    }
}
// MIT License - Copyright (c) 2026 Michael Hugh Harrington. Permission is hereby granted, free of charge, to any
// person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions: The above copyright notice and this permission notice
// shall be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS",
// WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
