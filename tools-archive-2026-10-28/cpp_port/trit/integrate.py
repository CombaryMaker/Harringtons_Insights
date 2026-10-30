t=open('part_trit.inc').read()
t=t.replace("static int tt_build(TTree *t, const int *vals, int nv, int d, const int *split) {","static int tt_build(TTree *t, const int *vals, int nv, int d, const int *split, int xr) {")
t=t.replace("for (int i = 0; i < nv; i++) { int dg = (vals[i] / pow3(5 - d)) % 3; g[dg][gn[dg]++] = vals[i]; }","for (int i = 0; i < nv; i++) { int dg = ((vals[i] ^ xr) / pow3(5 - d)) % 3; g[dg][gn[dg]++] = vals[i]; }")
for a in ["g[k], gn[k]","g[k0], gn[k0]","g[k1], gn[k1]","g[F], gn[F]","g[o1], gn[o1]","g[o2], gn[o2]"]:
    t=t.replace("tt_build(t, %s, d + 1, split)"%a,"tt_build(t, %s, d + 1, split, xr)"%a)
t=t.replace("""// pattern: 6 digits, each 0..2 = the value split off first at that digit
static void tt_make(TTree *t, const int *split) {
    int vals[256];
    for (int v = 0; v < 256; v++) vals[v] = v;
    t->nn = 0;
    tt_build(t, vals, 256, 0, split);""","""// pattern: 6 digits, each 0..2 = the value split off first at that digit;
// xr: bits flipped before the base-3 conversion (255 = reverse the values)
static void tt_make(TTree *t, const int *split, int xr) {
    int vals[256];
    for (int v = 0; v < 256; v++) vals[v] = v;
    t->nn = 0;
    tt_build(t, vals, 256, 0, split, xr);""")
assert t.count('split, xr)')==7, t.count('split, xr)')
t=t.replace("}  // namespace vt",open('trit_file.inc').read()+"\n}  // namespace vt")
s=open('../v3/hcm.cpp').read()
assert 'namespace vt' not in s
i=s.index('}  // namespace v3')+len('}  // namespace v3')
s=s[:i]+'\n'+t+s[i:]
reps=[
('''static int decompress_any(const uint8_t *blob, size_t bn, uint8_t **out, size_t *outn) {
    if (v3::is_v3(blob, bn)) return v3::decompress(blob, bn, out, outn);''','''static int decompress_any(const uint8_t *blob, size_t bn, uint8_t **out, size_t *outn) {
    if (v3::is_v3(blob, bn)) return v3::decompress(blob, bn, out, outn);
    if (vt::is_trit(blob, bn)) return vt::decompress(blob, bn, out, outn);'''),
('''    printf("Compressing " PS "  (%s mode)\\n", base_name(in), mode == v3::MODE_MAX ? "max" : "fast");''','''    printf("Compressing " PS "  (%s)\\n", base_name(in), mode == MODE_TRIT ? "hcm-trit v1" : (mode == v3::MODE_MAX ? "max mode" : "fast mode"));'''),
('''    if (v3::compress(data, n, mode, &blob, &bn)) { printf("  Not enough memory.\\n"); free(data); return 1; }''','''    int crc_ = mode == MODE_TRIT ? vt::compress(data, n, &blob, &bn) : v3::compress(data, n, mode, &blob, &bn);
    if (crc_) { printf("  Not enough memory.\\n"); free(data); return 1; }'''),
('''static int name_has_max(const pchar *p) {
    const pchar *b = base_name(p);
    for (; b[0] && b[1] && b[2]; b++)
        if (plower(b[0]) == 'm' && plower(b[1]) == 'a' && plower(b[2]) == 'x') return 1;
    return 0;
}''','''static int name_has(const pchar *p, const char *w) {
    const pchar *b = base_name(p);
    size_t k = strlen(w);
    for (; *b; b++) {
        size_t i = 0;
        while (i < k && b[i] && plower(b[i]) == (pchar)w[i]) i++;
        if (i == k) return 1;
    }
    return 0;
}'''),
('''    // command line: hcm c IN OUT | hcm x IN OUT | hcm d IN OUT
    if (argc == 4 && (is_word(argv[1], 'c') || is_word(argv[1], 'x') || is_word(argv[1], 'd'))) {
        if (is_word(argv[1], 'd')) return do_decompress(argv[2], argv[3]);
        return do_compress(argv[2], argv[3], is_word(argv[1], 'x') ? v3::MODE_MAX : v3::MODE_FAST);
    }
    int mode = self && name_has_max(self) ? v3::MODE_MAX : v3::MODE_FAST;
    printf("HCM v3  (Harrington Compression Method)  -  %s mode\\n", mode == v3::MODE_MAX ? "max" : "fast");''','''    // command line: hcm c|x|t IN OUT (fast | max | trit) | hcm d IN OUT
    if (argc == 4 && (is_word(argv[1], 'c') || is_word(argv[1], 'x') || is_word(argv[1], 't') || is_word(argv[1], 'd'))) {
        if (is_word(argv[1], 'd')) return do_decompress(argv[2], argv[3]);
        return do_compress(argv[2], argv[3], is_word(argv[1], 'x') ? v3::MODE_MAX : (is_word(argv[1], 't') ? MODE_TRIT : v3::MODE_FAST));
    }
    int mode = self && name_has(self, "trit") ? MODE_TRIT : (self && name_has(self, "max") ? v3::MODE_MAX : v3::MODE_FAST);
    if (mode == MODE_TRIT) printf("hcm-trit v1  (Harrington Compression Method, ternary)\\n");
    else printf("HCM v3  (Harrington Compression Method)  -  %s mode\\n", mode == v3::MODE_MAX ? "max" : "fast");'''),
('static int is_word(const pchar *s, char c) {','enum { MODE_TRIT = 2 };\n\nstatic int is_word(const pchar *s, char c) {'),
('''// v3 writes files in its own fast format (see the v3 section) and can still
// read v2 files. Two modes: fast (about 32 MB of tables) and max (about
// 64 MB of tables and a second mixer, a little smaller files).''','''// v3 writes files in its own fast format (see the v3 section) and can still
// read v2 files. Two modes: fast (about 32 MB of tables) and max (about
// 64 MB of tables and a second mixer, a little smaller files).
// hcm-trit v1 (see the hcm-trit section) codes each byte as 6 base-3 digits
// with a chosen value swap; it suits structured binary data better.
// The program's file name picks the mode: "trit" in the name = hcm-trit,
// "max" = max mode, otherwise fast mode. Every mode reads every format.'''),
('''// Command line: hcm c IN OUT (fast) | hcm x IN OUT (max) | hcm d IN OUT''','''// Command line: hcm c IN OUT (fast) | hcm x IN OUT (max) | hcm t IN OUT (trit)
//               | hcm d IN OUT'''),
]
for a,b in reps:
    assert s.count(a)==1, a[:70]
    s=s.replace(a,b)
open('../v3/hcm.cpp','w').write(s)
print('lines', s.count('\n'))
