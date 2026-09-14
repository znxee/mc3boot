"""Emits mc3boot.ini from the generated tables.

The group names have to match what the loader compares against, so they are read
out of patches.h rather than written by hand. The state of each line comes from
config.h, so a freshly generated .ini reproduces exactly the build you have -
editing it is then a change, not a guess.

    python gera_ini.py [destination]
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PAYLOAD = os.path.join(HERE, '..', 'payload')

ROW = re.compile(r'\{\s*"([^"]+)",\s*\w+,\s*\d+,\s*\d+,\s*(\w+)\s*\}')


def groups(header):
    """[(name, macro)] in the order the loader walks them."""
    txt = io.open(header, encoding='utf-8').read()
    return ROW.findall(txt)


def enabled(config):
    txt = io.open(config, encoding='utf-8').read()
    return {m: v for m, v in
            re.findall(r'^#define\s+(MC3_\w+)\s+(\d+)\s*$', txt, re.M)}


def main():
    args = [a for a in sys.argv[1:] if a != '--release']
    # A release .ini must not ship whoever built it their own test setup: every
    # patch off, no mod named that is not in the box. The person installing gets
    # a clean slate and turns on what they want, instead of inheriting a
    # configuration they cannot see the reasoning for.
    release = '--release' in sys.argv

    dest = args[0] if args else os.path.join(HERE, 'mc3boot.ini')
    cfg = {} if release else enabled(os.path.join(PAYLOAD, 'config.h'))

    L = []
    W = L.append
    W('; mc3boot.ini - what to turn on, without rebuilding anything.')
    W(';')
    W('; A missing file, a missing section or a missing line all mean "leave as')
    W('; built". An .ini that only turns one thing off is a valid .ini.')
    W(';')
    W('; `/` and `\\` are interchangeable in a group name, so pasting a name')
    W('; straight out of a pnach works.')
    W(';')
    W('; Groups that write the same address are alternatives: enable only one.')
    W('; The loader re-checks this and prints a conflict if it finds two.')
    W('')
    W('[mods]')
    W('; Relocatable modules, loaded IN THE ORDER written here. A name with no')
    W('; device uses the same one the .ini came from.')
    W(';')
    W('; To add a mod: copy its .mod next to this file and name it here. No')
    W('; compiler needed. To remove one: set it to 0, or delete the line.')
    W(';')
    W('; The value picks WHERE the module lives, and the cave is only 10072')
    W('; bytes, so it matters:')
    W(';')
    W(';   1            in the cave, alive from boot. Costs its whole size.')
    W(';   defer        body on the game heap, loaded on the first frame. Costs')
    W(';                no cave at all, but cannot hook anything that happens')
    W(';                before that frame.')
    W(';   shim         body on the heap like defer, but loaded during Main')
    W(';                init (001A0F28) instead of on the first frame, and the')
    W(';                hook sites are taken at boot by a 24-byte trampoline')
    W(';                each. This is what a hooked mod wants.')
    W(';   bootstrap    cave like 1, but keeps the per-frame callback a hooked')
    W(';                module normally gives up. Only core.mod needs it.')
    W(';   defer_once   as defer, with the entry called once instead of per')
    W(';   shim_once    frame. Only affects modules with no hooks.')
    W(';')
    W('; The .mod file is the same in every case - only this line changes.')
    W(';')
    W('; If any module is listed, the older single mc3mod.bin is ignored - both')
    W('; want the same address, and the .ini is what you asked for explicitly.')
    if release:
        W('; example.mod = 1')
    else:
        W('peds.mod = 1')
    W('')
    W('[payload]')
    W('; Behaviour that no constant can express, so it lives in compiled code.')
    W('; These apply to whichever module implements them.')
    W(';')
    W(';   dynamic_peds       animation rate = base * dt * 30, correct at any')
    W(';                      frame rate rather than only at exactly 60')
    W(';   dynamic_city_rate  calls mcCity::SetIntendedFrameRate with the')
    W(';                      measured rate instead of a number picked up front')
    W('dynamic_peds = %s' % cfg.get('MC3_DYNAMIC_PEDS', '0'))
    W('dynamic_city_rate = %s' % cfg.get('MC3_DYNAMIC_CITY_RATE', '0'))
    W('')
    W('[patches]')

    # The HostFS bootstrap is deliberately NOT here. It is not a choice: without
    # it the game reads the disc, and a generated `= 0` would break booting in a
    # way that looks like the loader failing. The loader decides it from the
    # device the game came from.
    section = None
    for name, macro in groups(os.path.join(PAYLOAD, 'patches.h')):
        top = name.split('/')[0]
        if top != section:
            W('')
            section = top
        W('%s = %s' % (name, cfg.get(macro, '0')))
    W('')
    W('; The HostFS bootstrap is not listed here: it is not optional. The loader')
    W('; applies it when the game came from host0: and skips it when the game')
    W('; came from a disc. There is nothing to choose.')

    txt = '\n'.join(L) + '\n'
    io.open(dest, 'w', encoding='latin1', newline='\n').write(txt)
    n = sum(1 for l in L if '=' in l and not l.startswith(';'))
    print('%s: %d settings' % (dest, n))


if __name__ == '__main__':
    main()
