// -----------------------------------------------------------------------------
//  mc3_sio.h - the terminal a module can talk out of, read from the PCSX2 log
//
//  One byte written to 0x1000F180 appears in emulog.txt. That is the whole
//  mechanism, and it is worth more than it looks: it needs no PINE, no
//  savestate, no screen and no action from whoever is watching. A module can
//  report while the game is still booting, and the loop closes in seconds -
//  measured on the city fork's dump, 498 KB out in 0.098 s of emulated time.
//
//  Compare the alternatives, both of which were tried and both of which cost a
//  session each: PINE in this build accepts a connection and then stops
//  answering, and a savestate needs PINE to trigger it.
//
//  WHY EVERY CHARACTER HERE IS A NUMBER
//
//  A module is packed by mc3_mkmod, which refuses a build with "LO16 sem HI16"
//  when a `%lo` relocation has no `%hi` partner - and GCC produces exactly that
//  when it shares one `lui` across several string constants. Writing `put(66)`
//  instead of `put('B')` removes the data base entirely: the city fork's
//  mesh_pck_dump closes with 66 jal26 relocations and ZERO hi/lo. Keep it that
//  way. Nothing in this header is a string.
//
//  THE BLOB FORMAT, which mc3_heap_dump.py on the PC side already reads:
//
//      BLOB <base:8hex> <length:8hex>
//      <offset:8hex>:<32 bytes as 64 hex chars>
//      ...
//      ENDB <length:8hex>
//
//  Rows are 32 bytes so a dump stays readable and a truncated line is obvious.
// -----------------------------------------------------------------------------
#ifndef MC3_SIO_H
#define MC3_SIO_H

#define MC3_SIO_PORT 0x1000F180

static __attribute__((noinline)) void mc3_sio_putc(int c)
{
    *(volatile unsigned char *)MC3_SIO_PORT = (unsigned char)c;
}

static void mc3_sio_nl(void) { mc3_sio_putc(10); }
static void mc3_sio_sp(void) { mc3_sio_putc(32); }

static void mc3_sio_nib(unsigned d)
{
    mc3_sio_putc(d < 10u ? (int)(48u + d) : (int)(55u + d));
}

static void mc3_sio_hex8(mc3_u32 v)
{
    for (int i = 28; i >= 0; i -= 4)
        mc3_sio_nib((v >> i) & 0xFu);
}

static void mc3_sio_hex2(unsigned b)
{
    mc3_sio_nib((b >> 4) & 0xFu);
    mc3_sio_nib(b & 0xFu);
}

static void mc3_sio_dec(mc3_u32 v)
{
    char t[12];
    int n = 0;
    if (!v) {
        mc3_sio_putc(48);
        return;
    }
    while (v && n < 11) {
        t[n++] = (char)(48u + (v % 10u));
        v /= 10u;
    }
    while (n)
        mc3_sio_putc(t[--n]);
}

// A four-character tag followed by a space. Callers pass character CODES, which
// is the point - `mc3_sio_tag4(66, 76, 79, 66)` is "BLOB" and costs nothing.
static void mc3_sio_tag4(int a, int b, int c, int d)
{
    mc3_sio_putc(a); mc3_sio_putc(b); mc3_sio_putc(c); mc3_sio_putc(d);
    mc3_sio_sp();
}

// A progress marker on its own line: four letters and a number. Use it the way
// a printf would be used, at each stage worth knowing about.
static void mc3_sio_mark(int a, int b, int c, int d, mc3_u32 value)
{
    mc3_sio_tag4(a, b, c, d);
    mc3_sio_hex8(value);
    mc3_sio_nl();
}

// The dump itself. `base` and `len` are what mc3_heap_base and mc3_heap_used
// gave you, and the pair goes out in the header line so the PC side does not
// have to be told separately.
static void mc3_sio_blob(mc3_u32 base, mc3_u32 len)
{
    mc3_sio_tag4(66, 76, 79, 66);           /* BLOB */
    mc3_sio_hex8(base);
    mc3_sio_sp();
    mc3_sio_hex8(len);
    mc3_sio_nl();

    for (mc3_u32 off = 0; off < len; off += 32u) {
        mc3_sio_hex8(off);
        mc3_sio_putc(58);                   /* : */
        for (mc3_u32 k = 0; k < 32u && off + k < len; ++k)
            mc3_sio_hex2(*(volatile unsigned char *)(base + off + k));
        mc3_sio_nl();
    }

    mc3_sio_tag4(69, 78, 68, 66);           /* ENDB */
    mc3_sio_hex8(len);
    mc3_sio_nl();
}

#endif /* MC3_SIO_H */
