/* ---------------------------------------------------------------------------
 *  mc3_mod.h - what a mod includes.
 *
 *  A module used to be able to run in exactly one place: a callback the loader
 *  invoked once per frame. Anything else had to be done by polling from there,
 *  which is a poor way to react to something that happens inside a specific
 *  function.
 *
 *  Now a module declares where it wants to run, and the loader installs it. That
 *  is possible because the loader writes into the game image before the game
 *  starts - from inside the running game the same trick means patching code
 *  under your own feet.
 *
 *      MC3_HOOK(0x001A385C, my_handler);
 *
 *  The address must be a `jal` in the game. The loader replaces it with a `jal`
 *  to your handler, which means YOUR HANDLER IS NOW RESPONSIBLE FOR CALLING THE
 *  ORIGINAL. That is deliberate: it is one line, it is visible, and it lets you
 *  run before it, after it, or instead of it.
 *
 *      static int my_handler()
 *      {
 *          int r = MC3_CALL(int, 0x00433490)();   // the original
 *          ...your work...
 *          return r;                              // the game reads this
 *      }
 *
 *  The delay slot of the original `jal` still executes, before your handler, as
 *  it did before - it belongs to the game and is none of our business.
 *
 *  A module with no MC3_HOOK at all still gets called once per frame, the way it
 *  always did. Declaring hooks is a way to ask for more, not a new obligation.
 * ------------------------------------------------------------------------- */
#ifndef MC3_MOD_H
#define MC3_MOD_H

typedef unsigned char  mc3_u8;
typedef unsigned short mc3_u16;
typedef unsigned int   mc3_u32;

/* One entry per hook. The function pointer is an ordinary absolute address, so
 * the linker emits an R_MIPS_32 for it and the loader's relocation pass fixes it
 * up like any other - the hook table needs no special handling to be movable. */
struct mc3_hook_entry {
    mc3_u32 target;             /* address of a `jal` in the game */
    void  (*handler)();         /* what to call instead */
};

/* `used` keeps it through -O2 (nothing references it), `section` puts every
 * entry in one array the build tool can find by name.
 *
 * The handler is cast rather than re-declared: most of them return something -
 * the game reads the original call's return value - and a fixed `void fn()`
 * declaration here would collide with every one of those. */
#define MC3_HOOK(addr, fn)                                                    \
    __attribute__((section(".mc3hooks"), used))                               \
    static const mc3_hook_entry mc3_hook_##fn##_##addr =                      \
        { (mc3_u32)(addr), (void (*)())(fn) }

/* Calling into the game. Addresses are fixed for a given executable, so this is
 * just a typed jump - there is no symbol table to look anything up in. */
#define MC3_CALL(ret, addr) ((ret (*)())(addr))
#define MC3_CALL1(ret, addr, t1) ((ret (*)(t1))(addr))
#define MC3_CALL2(ret, addr, t1, t2) ((ret (*)(t1, t2))(addr))
#define MC3_CALL3(ret, addr, t1, t2, t3) ((ret (*)(t1, t2, t3))(addr))
#define MC3_CALL4(ret, addr, t1, t2, t3, t4) ((ret (*)(t1, t2, t3, t4))(addr))
/* Five works because the ABI here is n32: the first EIGHT integer arguments go
 * in registers, a0-a3 then t0-t3. A five-argument game function reads its fifth
 * from t0, and a five-argument call from C puts it there. */
#define MC3_CALL5(ret, addr, t1, t2, t3, t4, t5) ((ret (*)(t1, t2, t3, t4, t5))(addr))
#define MC3_CALL6(ret, addr, t1, t2, t3, t4, t5, t6) ((ret (*)(t1, t2, t3, t4, t5, t6))(addr))

/* ---------------------------------------------------------------------------
 *  Memory
 *
 *  Ask the GAME for memory, at run time. Do not try to reserve a range before
 *  the game starts - that was tried and it does not work. Raising the heap base
 *  so the allocator could not reach a range still left the asset loader placing
 *  .pck images there, and a module got overwritten by wheel-rim data.
 *
 *  Once the game is running, its allocator is the authority: what it hands out,
 *  it will not hand to anything else. __builtin_new is the game's own operator
 *  new - 1333 call sites in the executable - so this is the same memory the game
 *  gives itself, with the same guarantees.
 *
 *  Not usable before the heap is up. Call it from a hook, not from static
 *  initialisation, and check the result.
 * ------------------------------------------------------------------------- */
#define MC3_BUILTIN_NEW     0x003B14D0u
#define MC3_BUILTIN_DELETE  0x003B1588u

static inline void *mc3_alloc(mc3_u32 size)
{
    return MC3_CALL1(void *, MC3_BUILTIN_NEW, mc3_u32)(size);
}

static inline void mc3_free(void *p)
{
    if (p) MC3_CALL1(void, MC3_BUILTIN_DELETE, void *)(p);
}

/* Runtime feature flags, written by the loader from mc3boot.ini. Absent on the
 * older patch-file routes, where a mod should keep its compiled-in default. */
#define MC3_CFG_ADDR   0x0061C9F0u
#define MC3_CFG_MAGIC  0x4D433343u

static inline int mc3_feature(mc3_u32 bit, int fallback)
{
    if (*(volatile mc3_u32 *)MC3_CFG_ADDR != MC3_CFG_MAGIC)
        return fallback;
    return (*(volatile mc3_u32 *)(MC3_CFG_ADDR + 4) & bit) != 0;
}

#endif
