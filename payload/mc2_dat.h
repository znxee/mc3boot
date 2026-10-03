/* ---------------------------------------------------------------------------
 *  mc2_dat.h - the MC2 files the city mods read, opened straight from MC2's
 *  own ASSETS.DAT, so an install needs the user's MC2 archive, not files
 *  extracted from it.
 *
 *  The archive is Angel's "Dave" (dave.py by Edness): "Dave", entries, info
 *  size, name size; at 0x800 one 16-byte record per entry (name offset, file
 *  offset, size, stored size; stored < size = raw deflate); the names, 6-bit
 *  packed with prefix reuse, start at 0x800 + info size. It is looked for as
 *  \MC2.DAT in the disc root when the game runs from disc (the mounted MC3
 *  ASSETS.DAT is the file backend) and as host0:MC2.DAT under HostFS.
 *  From disc it is read by sectors, the way datPageFile::Mount reads: the EE
 *  directory cache (built from the disc's own directories, 0x246350) gives
 *  the LBA and sceCdRead (0x245680, synchronous) the sectors. sceOpen on
 *  cdrom0: does not find a root file the game does not know - MC2.DAT is
 *  there, "MDIX 0 101" - while ASSETS.DAT opens.
 *
 *  Paths stay the ones mc2_city_config.h hands out (relative, mc2/...):
 *      mc2/<city>/city/<f>  ->  city/<city>/<f>
 *      mc2/tune/<f>         ->  tune/<f>
 *      mc2/anim/<f>         ->  anim/<f>
 *      mc2/bound/<f>        ->  bound/<f>
 *      mc2/<city>/<f>       ->  resource/<city>/<f>
 *  Only those entries are indexed (resource/ and city/ of losangeles and
 *  paris, tune/phys/, and the LA/Paris props' anim/, bound/ and
 *  tune/banger/ files: 1791 of 20021), as {FNV-1a of the name, offset, size,
 *  stored size}.
 *
 *  Reading goes through the game's raw file layer (Stream::Open(path, false)):
 *  a large read is one disc command there, where the zip layer cuts every
 *  read into 2048-byte commands. A deflated entry is inflated forward with
 *  the game's own zlib 1.1.3 (inflateInit2 -15, z_stream of 72 bytes); a seek
 *  forward inflates and drops, a seek backward starts the entry over.
 *  md_slurp keeps a whole file in memory, md_keep a set of ranges (one pass).
 *
 *  Without MC2.DAT nothing changes: from disc a stored entry of the MC3
 *  ASSETS.DAT is still read as a raw range (zipFile::Locate), on HostFS the
 *  loose file opens as before.
 *
 *  The including mod defines first:
 *      static void *md_alloc(mc3_u32 bytes);
 *      static void md_free(void *p);
 *      static int md_room(mc3_u32 bytes);      // heap has `bytes` to spare
 *  and uses md_open / md_read / md_seek / md_size / md_close for every file
 *  it opens with md_open (the handle is the game's Stream*).
 * ------------------------------------------------------------------------- */
#ifndef MC2_DAT_H
#define MC2_DAT_H

enum {
    MD_STREAM_OPEN = 0x003991F0, MD_STREAM_READ = 0x003993A8,
    MD_STREAM_SEEK = 0x003996C0, MD_STREAM_SIZE = 0x003997A8,
    MD_STREAM_CLOSE = 0x00399748, MD_MEMCPY = 0x00432B20,
    MD_INFLATE_INIT2 = 0x004FB4B0, MD_INFLATE = 0x004FB5F8,
    MD_INFLATE_END = 0x004FB440, MD_INFLATE_RESET = 0x004FB3E0,
    MD_FILE_BACKEND = 0x00618020, MD_ZIP_METHODS = 0x00619F58,
    MD_ZIP_SINGLE = 0x00619F44, MD_ZIP_LIST = 0x00619F4C, MD_ZIP_LOCATE = 0x004FAED8,
    MD_FILES = 8, MD_ZSTREAM = 80, MD_INBUF = 32768, MD_SCRATCH = 4096,
    MD_MAX_KEEP = 2048, MD_MAX_RANGES = 64, MD_NAME = 160,
    MD_DISC_LOCATE = 0x00246350, MD_DISC_READ = 0x00245680, MD_FLUSH_CACHE = 0x00546C20,
    MD_SECTORS = 16
};
struct md_entry { mc3_u32 hash, off, full, comp; };
struct md_range { mc3_u32 from, to, at; };
struct md_file {
    mc3_u32 h, base, size, csize;   // Stream on the archive; entry offset and sizes
    mc3_u32 pos, phys;              // next logical read; where the source stands
    mc3_u32 zmem, zs;               // inflate block and its z_stream (0 = stored)
    mc3_u32 in_used;                // compressed bytes handed to inflate
    mc3_u32 mem, buf;               // whole file in memory (md_slurp)
    mc3_u32 kmem, kdata, nkeep;     // kept ranges (md_keep)
    md_range *keep;
    mc3_u32 disc;                   // MC2.DAT's first LBA when read by sectors (0 = Stream)
};
struct md_state {
    md_file f[MD_FILES];
    mc3_u32 index_mem, index_n, index_tried;
    md_entry *index;
    mc3_u32 disc_lba, disc_size;         // MC2.DAT on disc (0 = not found)
    mc3_u32 secmem, sec, sec_lba, sec_n; // sector buffer and what it holds
};
static md_state g_md_state;
static __attribute__((noinline)) md_state *md_st() { return &g_md_state; }
static __attribute__((noinline)) const char *md_dat_disc() { static const char s[] = "cdrom0:\\MC2.DAT"; return s; }
static __attribute__((noinline)) const char *md_dat_host() { static const char s[] = "host0:MC2.DAT"; return s; }
static __attribute__((noinline)) const char *md_assets_disc() { static const char s[] = "cdrom0:\\ASSETS.DAT;1"; return s; }
static __attribute__((noinline)) const char *md_zlib_version() { static const char s[] = "1.1.3"; return s; }
// dave.py's CHARS: the 6-bit name alphabet
static __attribute__((noinline)) const char *md_chars() {
    static const char s[64] = { 0, ' ', '#', '$', '(', ')', '-', '.', '/', '?',
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '_',
        'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',
        'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '~', 0x7F };
    return s;
}
// Prefixes worth indexing (the rest of MC2's 20021 entries is never asked for).
static __attribute__((noinline)) const char *md_keep_prefixes() {
    static const char s[] = "resource/losangeles/\0resource/paris/\0city/losangeles/\0"
                            "city/paris/\0tune/phys/\0anim/l_prop_\0anim/p_prop_\0"
                            "bound/l_prop_\0bound/p_prop_\0tune/banger/l_\0tune/banger/p_\0";
    return s;
}

static __attribute__((noinline)) const char *md_s_tune() { static const char s[] = "tune/"; return s; }
static __attribute__((noinline)) const char *md_s_city() { static const char s[] = "city/"; return s; }
static __attribute__((noinline)) const char *md_s_resource() { static const char s[] = "resource/"; return s; }
static __attribute__((noinline)) const char *md_s_anim() { static const char s[] = "anim/"; return s; }
static __attribute__((noinline)) const char *md_s_bound() { static const char s[] = "bound/"; return s; }
static __attribute__((noinline)) const char *md_s_slash() { static const char s[] = "/"; return s; }

static void md_put(char c) { *(volatile mc3_u8 *)0x1000F180 = (mc3_u8)c; }
static void md_mark(char a, char b, char c, char d, mc3_u32 x, mc3_u32 y) {
    md_put(a); md_put(b); md_put(c); md_put(d); md_put(' ');
    for (int i = 28; i >= 0; i -= 4) { const mc3_u32 v = (x >> i) & 15u; md_put((char)(v < 10u ? '0' + v : 'A' + v - 10u)); }
    md_put(' ');
    for (int i = 28; i >= 0; i -= 4) { const mc3_u32 v = (y >> i) & 15u; md_put((char)(v < 10u ? '0' + v : 'A' + v - 10u)); }
    md_put('\n');
}

static mc3_u32 md_hash(const char *p) {
    mc3_u32 h = 0x811C9DC5u;
    while (*p) { h ^= (mc3_u8)*p++; h *= 0x01000193u; }
    return h;
}
static int md_starts(const char *s, const char *pre) {
    while (*pre) if (*s++ != *pre++) return 0;
    return 1;
}
static int md_copy(char *d, int at, const char *s, int max) {
    while (*s && at < max - 1) d[at++] = *s++;
    d[at] = 0;
    return at;
}
// mc2/... -> the name inside MC2's archive (0 when the path is not mc2/)
static int md_map(const char *path, char *out) {
    if (!(path[0] == 'm' && path[1] == 'c' && path[2] == '2' && path[3] == '/')) return 0;
    const char *rest = path + 4;
    if (md_starts(rest, md_s_tune()) || md_starts(rest, md_s_anim()) || md_starts(rest, md_s_bound())) {
        md_copy(out, 0, rest, MD_NAME);
        return 1;
    }
    int i = 0;
    while (rest[i] && rest[i] != '/') ++i;
    if (!rest[i] || i > 40) return 0;
    char city[48];
    for (int k = 0; k < i; ++k) city[k] = rest[k];
    city[i] = 0;
    const char *file = rest + i + 1;
    int n = 0;
    if (md_starts(file, md_s_city())) {
        n = md_copy(out, n, md_s_city(), MD_NAME); n = md_copy(out, n, city, MD_NAME);
        n = md_copy(out, n, md_s_slash(), MD_NAME); md_copy(out, n, file + 5, MD_NAME);
    } else {
        n = md_copy(out, n, md_s_resource(), MD_NAME); n = md_copy(out, n, city, MD_NAME);
        n = md_copy(out, n, md_s_slash(), MD_NAME); md_copy(out, n, file, MD_NAME);
    }
    return 1;
}
static int md_disc() { return *(volatile mc3_u32 *)MD_FILE_BACKEND == MD_ZIP_METHODS; }
static mc3_u32 md_raw_open(const char *path) {
    return MC3_CALL2(mc3_u32, MD_STREAM_OPEN, const char *, int)(path, 0);
}
static int md_raw_seek(mc3_u32 h, mc3_u32 at) {
    return MC3_CALL2(int, MD_STREAM_SEEK, mc3_u32, int)(h, (int)at) >= 0;
}
static int md_raw_read(mc3_u32 h, mc3_u32 dst, mc3_u32 n) {
    return MC3_CALL3(mc3_u32, MD_STREAM_READ, mc3_u32, mc3_u32, mc3_u32)(h, dst, n) == n;
}
static void md_memcpy(mc3_u32 d, mc3_u32 s, mc3_u32 n) {
    MC3_CALL3(void, MD_MEMCPY, mc3_u32, mc3_u32, mc3_u32)(d, s, n);
}
static mc3_u32 md_rd32(const mc3_u8 *p) {
    return (mc3_u32)p[0] | ((mc3_u32)p[1] << 8) | ((mc3_u32)p[2] << 16) | ((mc3_u32)p[3] << 24);
}

static void md_flush_dcache() { MC3_CALL1(void, MD_FLUSH_CACHE, int)(0); }
// `count` sectors at `lba` into `buf` (64-aligned), synchronously. The data
// cache is written back before (no dirty line may land on the DMA'd bytes
// later) and again after (no stale line may hide them).
static void md_disc_sectors(mc3_u32 lba, mc3_u32 count, mc3_u32 buf) {
    md_flush_dcache();
    MC3_CALL4(int, MD_DISC_READ, mc3_u32, mc3_u32, mc3_u32, int)(lba, count, buf, 0);
    md_flush_dcache();
}
// n bytes at byte `at` of the file starting at sector `lba0`
static int md_disc_read(mc3_u32 lba0, mc3_u32 at, mc3_u32 dst, mc3_u32 n) {
    md_state *m = md_st();
    if (!m->sec) {
        if (!md_room(MD_SECTORS * 2048u + 4096u)) return 0;
        void *b = md_alloc(MD_SECTORS * 2048u + 64u);
        if (!b) return 0;
        m->secmem = (mc3_u32)b;
        m->sec = ((mc3_u32)b + 63u) & ~63u;
        m->sec_n = 0u;
    }
    while (n) {
        const mc3_u32 s = lba0 + (at >> 11), off = at & 2047u;
        if (m->sec_n && s >= m->sec_lba && s < m->sec_lba + m->sec_n) {
            mc3_u32 c = ((m->sec_lba + m->sec_n - s) << 11) - off;
            if (c > n) c = n;
            md_memcpy(dst, m->sec + ((s - m->sec_lba) << 11) + off, c);
            dst += c; at += c; n -= c;
            continue;
        }
        if (!off && n >= 2048u && !(dst & 63u)) {          // whole sectors: straight in
            mc3_u32 cnt = n >> 11;
            if (cnt > 256u) cnt = 256u;
            md_disc_sectors(s, cnt, dst);
            dst += cnt << 11; at += cnt << 11; n -= cnt << 11;
            continue;
        }
        mc3_u32 cnt = (off + n + 2047u) >> 11;
        if (cnt > MD_SECTORS) cnt = MD_SECTORS;
        md_disc_sectors(s, cnt, m->sec);
        m->sec_lba = s; m->sec_n = cnt;
    }
    return 1;
}
// MC2.DAT in the disc root, through the game's directory cache
static int md_disc_dat() {
    md_state *m = md_st();
    if (m->disc_lba) return 1;
    mc3_u32 lba = 0, size = 0;
    if (!MC3_CALL3(int, MD_DISC_LOCATE, const char *, mc3_u32 *, mc3_u32 *)(md_dat_disc(), &lba, &size) || !lba)
        return 0;
    m->disc_lba = lba; m->disc_size = size;
    return 1;
}
// bytes of the archive: by sectors from disc, or through a Stream
static int md_arch_read(mc3_u32 disc_lba, mc3_u32 h, mc3_u32 at, mc3_u32 dst, mc3_u32 n) {
    if (disc_lba) return md_disc_read(disc_lba, at, dst, n);
    return md_raw_seek(h, at) && md_raw_read(h, dst, n);
}

// Read MC2.DAT's directory once and keep the entries the mods use.
static md_entry *md_index() {
    md_state *m = md_st();
    if (m->index || m->index_tried) return m->index;
    m->index_tried = 1u;
    const mc3_u32 dl = md_disc() && md_disc_dat() ? md_st()->disc_lba : 0u;
    const mc3_u32 h = dl ? 0u : (md_disc() ? 0u : md_raw_open(md_dat_host()));
    if (!dl && !h) { md_mark('M', 'D', 'I', 'X', 0u, 0x100u | (mc3_u32)md_disc()); return 0; }
    mc3_u32 stage = 1u;
    mc3_u8 head[16] __attribute__((aligned(16)));
    md_entry *out = 0;
    mc3_u32 kept = 0, tmp = 0;
    do {
        if (!md_arch_read(dl, h, 0u, (mc3_u32)head, 16u)) break;
        const int dave = head[0] == 'D' && head[1] == 'a' && head[2] == 'v' && head[3] == 'e';
        const int plain = head[0] == 'D' && head[1] == 'A' && head[2] == 'V' && head[3] == 'E';
        stage = 2u;
        if (!dave && !plain) break;
        const mc3_u32 count = md_rd32(head + 4), info = md_rd32(head + 8), names = md_rd32(head + 12);
        const mc3_u32 toc = count * 16u;
        if (!count || toc > info || toc > 0x200000u || names > 0x200000u) break;
        stage = 3u;
        if (!md_room(toc + names + MD_MAX_KEEP * 16u + 256u)) break;
        stage = 4u;
        tmp = (mc3_u32)md_alloc(toc + names + 64u);
        out = tmp ? (md_entry *)md_alloc(MD_MAX_KEEP * 16u + 16u) : 0;
        if (!out) break;
        const mc3_u32 t = (tmp + 15u) & ~15u;
        const mc3_u8 *nb = (const mc3_u8 *)(t + toc);
        if (!md_arch_read(dl, h, 0x800u, t, toc) || !md_arch_read(dl, h, 0x800u + info, t + toc, names)) {
            md_free(out); out = 0; break;
        }
        stage = 5u;
        const char *chars = md_chars();
        const char *pres = md_keep_prefixes();
        char prev[MD_NAME], name[MD_NAME];
        prev[0] = 0;
        for (mc3_u32 i = 0; i < count; ++i) {
            const mc3_u8 *rec = (const mc3_u8 *)(t + i * 16u);
            mc3_u32 at = md_rd32(rec);
            int len = 0;
            if (plain) {
                while (at < names && nb[at] && len < MD_NAME - 1) name[len++] = (char)nb[at++];
            } else {
                mc3_u32 q[4]; int qn = 0, qi = 0;
                #define MD_BITS() do { if (at + 3u > names) { qn = 0; break; } \
                    const mc3_u32 v = nb[at] | (nb[at + 1] << 8) | (nb[at + 2] << 16); at += 3u; \
                    q[0] = v & 63u; q[1] = (v >> 6) & 63u; q[2] = (v >> 12) & 63u; q[3] = (v >> 18) & 63u; \
                    qn = 4; qi = 0; } while (0)
                MD_BITS();
                if (qn && q[0] >= 0x38u) {
                    int dedup = (int)(q[1] - 0x20u) * 8 + (int)q[0] - 0x38;
                    if (dedup < 0) dedup = 0;
                    while (len < dedup && prev[len] && len < MD_NAME - 1) { name[len] = prev[len]; ++len; }
                    qi = 2;
                }
                while (qn && q[qi] && len < MD_NAME - 1) {
                    name[len++] = chars[q[qi]];
                    if (++qi == qn) MD_BITS();
                }
                #undef MD_BITS
            }
            name[len] = 0;
            for (int k = 0; k <= len; ++k) prev[k] = name[k];
            if (kept >= MD_MAX_KEEP) continue;
            for (const char *p = pres; *p; ) {
                if (md_starts(name, p)) {
                    md_entry *e = &out[kept++];
                    e->hash = md_hash(name);
                    e->off = md_rd32(rec + 4); e->full = md_rd32(rec + 8); e->comp = md_rd32(rec + 12);
                    break;
                }
                while (*p) ++p;
                ++p;
            }
        }
    } while (0);
    if (tmp) md_free((void *)tmp);
    if (h) MC3_CALL1(void, MD_STREAM_CLOSE, mc3_u32)(h);
    if (out && !kept) { md_free(out); out = 0; }
    md_mark('M', 'D', 'I', 'X', kept, stage | ((mc3_u32)md_disc() << 8));
    m->index_mem = (mc3_u32)out;
    m->index = out;
    m->index_n = kept;
    return out;
}
// Index lookup for an mc2/ path; 1 with the entry, 0 when not in MC2.DAT.
static int md_find(const char *path, md_entry *found) {
    char name[MD_NAME];
    if (!md_map(path, name)) return 0;
    md_entry *e = md_index();
    if (!e) return 0;
    const mc3_u32 h = md_hash(name);
    const md_state *m = md_st();
    for (mc3_u32 i = 0; i < m->index_n; ++i)
        if (e[i].hash == h) { *found = e[i]; return 1; }
    return 0;
}
// A stored entry of the mounted MC3 ASSETS.DAT (disc only): offset, size.
static int md_zip_entry(const char *path, mc3_u32 *off, mc3_u32 *size) {
    if (!md_disc()) return 0;
    mc3_u32 e = 0, z = *(volatile mc3_u32 *)MD_ZIP_SINGLE;
    if (z) e = MC3_CALL2(mc3_u32, MD_ZIP_LOCATE, mc3_u32, const char *)(z, path);
    else
        for (z = *(volatile mc3_u32 *)MD_ZIP_LIST; z && !e; z = *(volatile mc3_u32 *)z)
            e = MC3_CALL2(mc3_u32, MD_ZIP_LOCATE, mc3_u32, const char *)(z, path);
    if (!e || *(volatile mc3_u32 *)(e + 8u) != *(volatile mc3_u32 *)(e + 12u)) return 0;
    *off = *(volatile mc3_u32 *)(e + 4u);
    *size = *(volatile mc3_u32 *)(e + 8u);
    return 1;
}
static md_file *md_of(mc3_u32 h) {
    if (!h) return 0;
    md_state *m = md_st();
    for (int k = 0; k < MD_FILES; ++k) if (m->f[k].h == h) return &m->f[k];
    return 0;
}
static md_file *md_free_slot() {
    md_state *m = md_st();
    for (int k = 0; k < MD_FILES; ++k) if (!m->f[k].h) return &m->f[k];
    return 0;
}
static void md_clear(md_file *f) {
    f->h = f->base = f->size = f->csize = f->pos = f->phys = 0u;
    f->zmem = f->zs = f->in_used = f->mem = f->buf = f->kmem = f->kdata = f->nkeep = 0u;
    f->keep = 0;
    f->disc = 0u;
}
static void md_end_inflate(md_file *f) {
    if (!f->zs) return;
    MC3_CALL1(int, MD_INFLATE_END, mc3_u32)(f->zs);
    md_free((void *)f->zmem);
    f->zs = f->zmem = 0u;
}
static int md_start_inflate(md_file *f) {
    if (!md_room(MD_ZSTREAM + MD_INBUF + MD_SCRATCH + 32768u + 8192u)) return 0;
    void *m = md_alloc(MD_ZSTREAM + MD_INBUF + MD_SCRATCH + 32u);
    if (!m) return 0;
    const mc3_u32 zs = ((mc3_u32)m + 15u) & ~15u;
    for (mc3_u32 i = 0; i < MD_ZSTREAM; i += 4u) *(mc3_u32 *)(zs + i) = 0u;
    if (MC3_CALL4(int, MD_INFLATE_INIT2, mc3_u32, int, const char *, int)
            (zs, -15, md_zlib_version(), 72) != 0) {
        md_free(m);
        return 0;
    }
    f->zmem = (mc3_u32)m; f->zs = zs; f->in_used = 0u; f->phys = 0u;
    return 1;
}

static mc3_u32 md_open(const char *path) {
    md_file *f = md_free_slot();
    md_entry e;
    mc3_u32 off = 0, size = 0;
    if (f && md_find(path, &e) && md_st()->disc_lba && md_disc()) {
        md_clear(f);
        f->h = (mc3_u32)f; f->disc = md_st()->disc_lba;
        f->base = e.off; f->size = e.full; f->csize = e.comp;
        if (e.comp == e.full || md_start_inflate(f)) return f->h;
        md_clear(f);
    } else if (f && md_find(path, &e)) {
        const mc3_u32 h = md_raw_open(md_dat_host());
        if (h) {
            md_clear(f);
            f->h = h; f->base = e.off; f->size = e.full; f->csize = e.comp;
            if ((e.comp == e.full || md_start_inflate(f)) && md_raw_seek(h, e.off)) return h;
            md_end_inflate(f);
            md_clear(f);
            MC3_CALL1(void, MD_STREAM_CLOSE, mc3_u32)(h);
        }
    }
    if (f && md_zip_entry(path, &off, &size)) {
        const mc3_u32 h = md_raw_open(md_assets_disc());
        if (h && md_raw_seek(h, off)) {
            md_clear(f);
            f->h = h; f->base = off; f->size = f->csize = size;
            return h;
        }
        if (h) MC3_CALL1(void, MD_STREAM_CLOSE, mc3_u32)(h);
    }
    return MC3_CALL2(mc3_u32, MD_STREAM_OPEN, const char *, int)(path, 1);
}
static int md_size(mc3_u32 h) {
    const md_file *f = md_of(h);
    return f ? (int)f->size : MC3_CALL1(int, MD_STREAM_SIZE, mc3_u32)(h);
}
static int md_seek(mc3_u32 h, mc3_u32 pos) {
    md_file *f = md_of(h);
    if (f) { f->pos = pos; return (int)pos; }
    return MC3_CALL2(int, MD_STREAM_SEEK, mc3_u32, int)(h, (int)pos);
}
// n bytes of inflated output at the current source position into dst
static int md_inflate_to(md_file *f, mc3_u32 dst, mc3_u32 n) {
    const mc3_u32 zs = f->zs, in = zs + MD_ZSTREAM;
    *(volatile mc3_u32 *)(zs + 16u) = dst;
    *(volatile mc3_u32 *)(zs + 20u) = n;
    while (*(volatile mc3_u32 *)(zs + 20u)) {
        if (!*(volatile mc3_u32 *)(zs + 4u) && f->in_used < f->csize) {
            mc3_u32 k = f->csize - f->in_used;
            if (k > MD_INBUF) k = MD_INBUF;
            if (f->disc ? !md_disc_read(f->disc, f->base + f->in_used, in, k)
                        : !md_raw_read(f->h, in, k)) return 0;
            f->in_used += k;
            *(volatile mc3_u32 *)(zs + 0u) = in;
            *(volatile mc3_u32 *)(zs + 4u) = k;
        }
        // With all input taken inflate may still hold output (the last block
        // ran into a full output buffer): call it again, give up only when it
        // makes no progress and there is nothing left to feed it.
        const mc3_u32 before = *(volatile mc3_u32 *)(zs + 20u);
        const int r = MC3_CALL2(int, MD_INFLATE, mc3_u32, int)(zs, 0);
        if (r == 1) { if (*(volatile mc3_u32 *)(zs + 20u)) return 0; break; }
        if (r < 0 && r != -5) return 0;
        if (*(volatile mc3_u32 *)(zs + 20u) == before && !*(volatile mc3_u32 *)(zs + 4u) &&
            f->in_used >= f->csize) return 0;
    }
    f->phys += n;
    return 1;
}
static int md_source_read(md_file *f, mc3_u32 dst, mc3_u32 n) {
    if (f->pos > f->size || n > f->size - f->pos) return 0;
    if (!f->zs) {                                        // stored: a range of the archive
        if (f->disc) { if (!md_disc_read(f->disc, f->base + f->pos, dst, n)) return 0; }
        else {
            if (f->phys != f->pos && !md_raw_seek(f->h, f->base + f->pos)) return 0;
            if (!md_raw_read(f->h, dst, n)) return 0;
        }
        f->pos += n; f->phys = f->pos;
        return 1;
    }
    if (f->pos < f->phys) {                              // backward: the entry over
        MC3_CALL1(int, MD_INFLATE_RESET, mc3_u32)(f->zs);
        *(volatile mc3_u32 *)(f->zs + 0u) = 0u;
        *(volatile mc3_u32 *)(f->zs + 4u) = 0u;
        f->in_used = 0u; f->phys = 0u;
        if (!f->disc && !md_raw_seek(f->h, f->base)) return 0;
    }
    const mc3_u32 scratch = f->zs + MD_ZSTREAM + MD_INBUF;
    while (f->phys < f->pos) {                           // forward: inflate and drop
        mc3_u32 k = f->pos - f->phys;
        if (k > MD_SCRATCH) k = MD_SCRATCH;
        if (!md_inflate_to(f, scratch, k)) return 0;
    }
    if (!md_inflate_to(f, dst, n)) return 0;
    f->pos += n;
    return 1;
}
static int md_read(mc3_u32 h, void *p, mc3_u32 n) {
    md_file *f = md_of(h);
    if (!f) return MC3_CALL3(mc3_u32, MD_STREAM_READ, mc3_u32, mc3_u32, mc3_u32)(h, (mc3_u32)p, n) == n;
    if (f->buf) {
        if (f->pos > f->size || n > f->size - f->pos) return 0;
        md_memcpy((mc3_u32)p, f->buf + f->pos, n);
        f->pos += n;
        return 1;
    }
    for (mc3_u32 k = 0; k < f->nkeep; ++k) {
        const md_range *r = &f->keep[k];
        if (f->pos >= r->from && f->pos + n <= r->to) {
            md_memcpy((mc3_u32)p, f->kdata + r->at + (f->pos - r->from), n);
            f->pos += n;
            return 1;
        }
    }
    return md_source_read(f, (mc3_u32)p, n);
}
// Up to n bytes (fewer at the end of the file); -1 on error.
static int md_read_some(mc3_u32 h, void *p, mc3_u32 n) {
    md_file *f = md_of(h);
    if (!f) return (int)MC3_CALL3(mc3_u32, MD_STREAM_READ, mc3_u32, mc3_u32, mc3_u32)(h, (mc3_u32)p, n);
    const mc3_u32 left = f->pos < f->size ? f->size - f->pos : 0u;
    if (n > left) n = left;
    if (!n) return 0;
    return md_read(h, p, n) ? (int)n : -1;
}
// The whole file in memory, when the heap has room; 1 when it is.
static int md_slurp(mc3_u32 h) {
    md_file *f = md_of(h);
    if (!f || f->buf || !f->size || !md_room(f->size + 64u)) return 0;
    void *m = md_alloc(f->size + 16u);
    if (!m) return 0;
    const mc3_u32 buf = ((mc3_u32)m + 15u) & ~15u, keep = f->pos;
    f->pos = 0u;
    if (!md_source_read(f, buf, f->size)) { md_free(m); f->pos = keep; return 0; }
    f->mem = (mc3_u32)m; f->buf = buf; f->pos = keep;
    md_end_inflate(f);
    return 1;
}
// Keep ranges [from[i], to[i]) in memory, read in one forward pass; reads that
// fall inside one of them are served from memory afterwards. 1 when kept.
static int md_keep(mc3_u32 h, const mc3_u32 *from, const mc3_u32 *to, mc3_u32 n) {
    md_file *f = md_of(h);
    if (!f || f->buf || f->kmem || !n || n > MD_MAX_RANGES) return 0;
    md_range tmp[MD_MAX_RANGES];
    mc3_u32 m = 0;
    for (mc3_u32 i = 0; i < n; ++i) {                    // insertion sort by start
        if (to[i] <= from[i] || to[i] > f->size) continue;
        mc3_u32 j = m++;
        while (j && tmp[j - 1].from > from[i]) { tmp[j] = tmp[j - 1]; --j; }
        tmp[j].from = from[i]; tmp[j].to = to[i];
    }
    mc3_u32 k = 0;                                       // merge overlapping / touching
    for (mc3_u32 i = 0; i < m; ++i) {
        if (k && tmp[i].from <= tmp[k - 1].to) {
            if (tmp[i].to > tmp[k - 1].to) tmp[k - 1].to = tmp[i].to;
        } else tmp[k++] = tmp[i];
    }
    mc3_u32 bytes = 0;
    for (mc3_u32 i = 0; i < k; ++i) { tmp[i].at = bytes; bytes += (tmp[i].to - tmp[i].from + 15u) & ~15u; }
    const mc3_u32 table = k * (mc3_u32)sizeof(md_range);
    if (!k || !md_room(bytes + table + 64u)) return 0;
    void *mem = md_alloc(bytes + table + 32u);
    if (!mem) return 0;
    const mc3_u32 data = ((mc3_u32)mem + 15u) & ~15u;
    md_range *r = (md_range *)(data + bytes);
    const mc3_u32 keep = f->pos;
    for (mc3_u32 i = 0; i < k; ++i) {
        r[i] = tmp[i];
        f->pos = tmp[i].from;
        if (!md_source_read(f, data + tmp[i].at, tmp[i].to - tmp[i].from)) {
            md_free(mem); f->pos = keep; return 0;
        }
    }
    f->kmem = (mc3_u32)mem; f->kdata = data; f->keep = r; f->nkeep = k; f->pos = keep;
    return 1;
}
static void md_close(mc3_u32 h) {
    md_file *f = md_of(h);
    int stream = 1;
    if (f) {
        stream = !f->disc;
        md_end_inflate(f);
        if (f->mem) md_free((void *)f->mem);
        if (f->kmem) md_free((void *)f->kmem);
        md_clear(f);
    }
    if (stream) MC3_CALL1(void, MD_STREAM_CLOSE, mc3_u32)(h);
}
// After the mod has freed every block itself (no md_free here): forget the
// index and the table. Inflate state lives in the game's heap and is ended by
// md_close, so close every file first.
static void md_forget() {
    md_state *m = md_st();
    for (int k = 0; k < MD_FILES; ++k) md_clear(&m->f[k]);
    m->index = 0; m->index_mem = m->index_n = m->index_tried = 0u;
    m->secmem = m->sec = m->sec_n = 0u;
}
// Free the index (a mod that does not mass-free its blocks).
static void md_release() {
    md_state *m = md_st();
    if (m->index_mem) md_free((void *)m->index_mem);
    m->index = 0; m->index_mem = m->index_n = m->index_tried = 0u;
    if (m->secmem) md_free((void *)m->secmem);
    m->secmem = m->sec = m->sec_n = 0u;
}

#endif
