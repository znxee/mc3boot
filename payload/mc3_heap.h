// -----------------------------------------------------------------------------
//  mc3_heap.h - the game's own private heap allocator, for any module
//
//  The engine has an API built for exactly this and it is better than freeing
//  the frontend: mcHeap::CreateMemTopHeap carves a piece off the TOP of the main
//  heap and hands back a memMemoryAllocator of its own. The piece is CONTIGUOUS
//  and PRIVATE, so you know where it starts and how much there is, and every
//  pointer in whatever you build lands inside it - which is precisely the shape
//  of a .pck body. Dump the heap and you have the body; the header is four
//  words.
//
//  THE ALLOCATOR IS TWO-ENDED. This corrects a layout that was written down
//  wrong once and cost a session: it is NOT "+0x08 size, +0x0C used".
//
//      +0x04   base
//      +0x08   top of free   - a POINTER, and it DESCENDS
//      +0x0C   cursor        - allocation, and it ASCENDS
//
//  CreateMemTopHeap does `lw a3,8(t0) / addiu a0,a3,-16 / sw a0,8(t0)`, and the
//  overrun test at 0x003B018C is `sltu [+0x08] < [+0x0C]`. Running out is the
//  two ends MEETING, and every memtop heap you carve moves the ceiling down for
//  whoever is still allocating from the main one. Free space is the gap between
//  them, and that subtraction was checked against the game's own figure: when
//  CreateMemTopHeap does not fit it prints "(%d bytes requested, %d bytes
//  available)", and the number matched exactly - 1456656 with the frontend up.
//
//  WHY THE NULL GUARD IS NOT OPTIONAL
//
//  mcHeap::CreateMemTopHeap opens with `*(dword_70E500 + 8)` and has NO check of
//  its own. Called before the game has an active allocator it reads address 8,
//  derives a base from the garbage and memsets from it - measured as 50 TLB
//  misses at 0x0042E760 storing from 0x02000000 up, the top of EE RAM, and then
//  PCSX2 died. A frame counter is not a readiness signal. mc3_heap_ready() is.
//
//  BUDGET: the main heap is 28 MB - Main 0x001A0E94 calls mcHeap::InitClass with
//  0x1C and memHeap::InitClass 0x005A87A8 does `a2 << 20`. With the frontend
//  resident there is roughly 1.4 MB free at the top; with it unloaded, ~11.9 MB.
//
//  ALSO MEASURED, and it is what makes one Push cover a whole operation:
//  datMemoryStartUseTemporary 0x0042E5C8 does NOT change allocator - it only
//  pushes two globals (dword_6D59CC, dword_6D59D0), bookkeeping and not heap.
//  So everything rmcModel::Create allocates, the parse buffer and the final
//  graph alike, comes out of the active allocator at dword_70E500.
//
//  RELOCATION DISCIPLINE: no string literals live here. The default heap name is
//  built from numeric character codes into a local array, so including this
//  header adds no hi/lo relocation to your module - see mc3_sio.h for why that
//  matters.
// -----------------------------------------------------------------------------
#ifndef MC3_HEAP_H
#define MC3_HEAP_H

enum {
    MC3_HEAP_CREATE_MEMTOP  = 0x004BD650,   // (bytes, name) -> memMemoryAllocator*
    MC3_HEAP_RELEASE_MEMTOP = 0x004BD770,   // gives the top back
    MC3_HEAP_PUSH           = 0x004BD5D8,   // two words: save active, install new
    MC3_HEAP_POP            = 0x004BD5F0,
    MC3_HEAP_ACTIVE         = 0x0070E500,   // dword_70E500, the current allocator
    MC3_HEAP_SAVED          = 0x00619D60,   // where PushHeap parks the old one

    MC3_ALLOC_BASE   = 0x04,
    MC3_ALLOC_TOP    = 0x08,    // descends
    MC3_ALLOC_CURSOR = 0x0C,    // ascends
};

// A game pointer, and word aligned. Used before every dereference below.
static __attribute__((noinline)) int mc3_ptr_ok(mc3_u32 p)
{
    return p >= 0x00100000u && p < 0x02000000u && (p & 3u) == 0u;
}

static __attribute__((noinline)) mc3_u32 mc3_heap_active(void)
{
    return *(volatile mc3_u32 *)MC3_HEAP_ACTIVE;
}

// The readiness signal. Wait for THIS, not for a number of frames.
static int mc3_heap_ready(void)
{
    const mc3_u32 a = mc3_heap_active();
    if (!mc3_ptr_ok(a))
        return 0;
    const mc3_u32 top = *(volatile mc3_u32 *)(a + MC3_ALLOC_TOP);
    const mc3_u32 cur = *(volatile mc3_u32 *)(a + MC3_ALLOC_CURSOR);
    // Contents have to be sane too, not just the pointer: a half-built
    // allocator reads as a valid pointer to zeros.
    return mc3_ptr_ok(*(volatile mc3_u32 *)(a + MC3_ALLOC_BASE))
           && top > cur && top <= 0x02000000u;
}

static mc3_u32 mc3_heap_free(mc3_u32 alloc)
{
    if (!mc3_ptr_ok(alloc))
        return 0u;
    const mc3_u32 top = *(volatile mc3_u32 *)(alloc + MC3_ALLOC_TOP);
    const mc3_u32 cur = *(volatile mc3_u32 *)(alloc + MC3_ALLOC_CURSOR);
    return top > cur ? top - cur : 0u;
}

static mc3_u32 mc3_heap_base(mc3_u32 alloc)
{
    return mc3_ptr_ok(alloc) ? *(volatile mc3_u32 *)(alloc + MC3_ALLOC_BASE) : 0u;
}

// How much of a private heap has actually been handed out - the length to dump.
static mc3_u32 mc3_heap_used(mc3_u32 alloc)
{
    return mc3_ptr_ok(alloc) ? *(volatile mc3_u32 *)(alloc + MC3_ALLOC_CURSOR) : 0u;
}

// Carve one. Returns 0 rather than letting the game walk off the end: the two
// tests below are the ones CreateMemTopHeap would fail at, done first.
static mc3_u32 mc3_heap_create_named(mc3_u32 bytes, const char *name)
{
    if (!mc3_heap_ready())
        return 0u;
    if (mc3_heap_free(mc3_heap_active()) < bytes + 16u)
        return 0u;
    return MC3_CALL2(mc3_u32, MC3_HEAP_CREATE_MEMTOP,
                     mc3_u32, const char *)(bytes, name);
}

// Same, with a name built out of numeric character codes so no module pays a
// relocation for it. Reads "modheap" in the game's memory statistics.
static mc3_u32 mc3_heap_create(mc3_u32 bytes)
{
    char nome[8];
    nome[0] = 109; nome[1] = 111; nome[2] = 100; nome[3] = 104;
    nome[4] = 101; nome[5] = 97;  nome[6] = 112; nome[7] = 0;
    return mc3_heap_create_named(bytes, nome);
}

static void mc3_heap_push(mc3_u32 h)
{
    if (mc3_ptr_ok(h))
        MC3_CALL1(void, MC3_HEAP_PUSH, mc3_u32)(h);
}

static void mc3_heap_pop(void)
{
    MC3_CALL(void, MC3_HEAP_POP)();
}

// Give the top back. Do NOT call this before dumping: releasing returns the
// range, and the next allocation writes straight over what you meant to keep.
static void mc3_heap_release(mc3_u32 h)
{
    if (mc3_ptr_ok(h))
        MC3_CALL1(void, MC3_HEAP_RELEASE_MEMTOP, mc3_u32)(h);
}

#endif /* MC3_HEAP_H */
