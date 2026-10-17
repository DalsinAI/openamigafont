#!/usr/bin/env python3
"""Generates src/of_unicode_data.c from the Unicode Character Database.

    tools/gen_unicode.py UCD_DIR [OUT]

UCD_DIR holds the UCD 16.0.0 files (https://www.unicode.org/Public/16.0.0/ucd/):
UnicodeData.txt, DerivedCoreProperties.txt, DerivedNormalizationProps.txt,
Scripts.txt. The data is the Unicode Consortium's, under the Unicode License
v3 (LICENSES/Unicode-License-v3.txt); this script is the AmigaChrome Team's,
MIT licence.

Per codepoint it records the general category, the canonical combining class,
whether it is a default ignorable, and its script, in two-stage tables (a
block index for each 128 codepoints, the blocks shared). It also writes the
canonical decompositions and the primary composites NFC builds.
"""
import os
import sys

UCD_VERSION = "16.0.0"
BLOCK = 64

CATEGORIES = ["Cn", "Lu", "Ll", "Lt", "Lm", "Lo", "Mn", "Mc", "Me", "Nd", "Nl", "No",
              "Pc", "Pd", "Ps", "Pe", "Pi", "Pf", "Po", "Sm", "Sc", "Sk", "So",
              "Zs", "Zl", "Zp", "Cc", "Cf", "Cs", "Co"]


def ranges(path):
    """(first, last, fields) for each data line of a UCD file"""
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            fields = [x.strip() for x in line.split(";")]
            cps = fields[0]
            if ".." in cps:
                a, b = cps.split("..")
                yield int(a, 16), int(b, 16), fields[1:]
            else:
                yield int(cps, 16), int(cps, 16), fields[1:]


def main():
    ucd = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), "..", "src", "of_unicode_data.c")
    n = 0x110000
    gc = [0] * n
    ccc = [0] * n
    decomp = {}
    # UnicodeData: ranges are given by First/Last pairs
    first = None
    with open(os.path.join(ucd, "UnicodeData.txt"), encoding="utf-8") as f:
        for line in f:
            p = line.rstrip("\n").split(";")
            cp = int(p[0], 16)
            cat = CATEGORIES.index(p[2])
            if p[1].endswith(", First>"):
                first = cp
                continue
            lo = first if p[1].endswith(", Last>") else cp
            first = None
            for c in range(lo, cp + 1):
                gc[c] = cat
                ccc[c] = int(p[3])
            if p[5] and not p[5].startswith("<"):
                decomp[cp] = [int(x, 16) for x in p[5].split()]
    ignorable = [0] * n
    for a, b, f in ranges(os.path.join(ucd, "DerivedCoreProperties.txt")):
        if f[0] == "Default_Ignorable_Code_Point":
            for c in range(a, b + 1):
                ignorable[c] = 1
    full_excl = set()
    for a, b, f in ranges(os.path.join(ucd, "DerivedNormalizationProps.txt")):
        if f[0] == "Full_Composition_Exclusion":
            full_excl.update(range(a, b + 1))
    script_names = ["Unknown", "Common", "Inherited"]
    script = [0] * n
    for a, b, f in ranges(os.path.join(ucd, "Scripts.txt")):
        name = f[0]
        if name not in script_names:
            script_names.append(name)
        k = script_names.index(name)
        for c in range(a, b + 1):
            script[c] = k
    common = sorted(script_names[3:])
    script_names = script_names[:3] + common
    # Unknown, Common and Inherited first, then the rest by name
    for a, b, f in ranges(os.path.join(ucd, "Scripts.txt")):
        k = script_names.index(f[0])
        for c in range(a, b + 1):
            script[c] = k
    assert len(script_names) < 256

    # records: (gc, ccc, ignorable) -> index
    records = [(0, 0, 0)]
    rec_index = {(0, 0, 0): 0}
    rec = [0] * n
    for c in range(n):
        key = (gc[c], ccc[c], ignorable[c])
        i = rec_index.get(key)
        if i is None:
            i = rec_index[key] = len(records)
            records.append(key)
        rec[c] = i
    assert len(records) < 256

    # Tables cover U+0000..U+3FFFF and U+E0000..U+E0FFF. Everything else is
    # one value per plane range, checked here: unassigned (Cn, Unknown), or
    # private use (Co, Unknown) in planes 15 and 16.
    def outside(c):
        if 0xF0000 <= c <= 0xFFFFD or 0x100000 <= c <= 0x10FFFD:
            return (CATEGORIES.index("Co"), 0, 0), 0
        return (0, 0, 0), 0
    for c in list(range(0x40000, 0xE0000)) + list(range(0xE1000, n)):
        assert ((gc[c], ccc[c], ignorable[c]), script[c]) == outside(c), hex(c)
    covered = list(range(0, 0x40000)) + list(range(0xE0000, 0xE1000))

    def two_stage(values):
        blocks, index, stage1 = [], {}, []
        for b in range(0, len(covered), BLOCK):
            b = covered[b]
            blk = tuple(values[b:b + BLOCK])
            i = index.get(blk)
            if i is None:
                i = index[blk] = len(blocks)
                blocks.append(blk)
            stage1.append(i)
        return stage1, blocks

    prop1, propb = two_stage(rec)
    scr1, scrb = two_stage(script)

    # canonical decompositions (one step) and primary composites
    dec = sorted(decomp.items())
    comp = []
    for c, d in dec:
        if len(d) == 2 and c not in full_excl:
            comp.append((d[0], d[1], c))
    comp.sort()

    def carray(name, ctype, vals, per=16):
        s = "static const %s %s[%d] = {\n" % (ctype, name, len(vals))
        for i in range(0, len(vals), per):
            s += "    " + ", ".join(str(v) for v in vals[i:i + per]) + ",\n"
        return s + "};\n\n"

    o = []
    o.append("/* Unicode character properties for OpenFont, generated by\n"
             " * tools/gen_unicode.py from the Unicode Character Database %s.\n"
             " * Do not edit; run the script again.\n"
             " *\n"
             " * The data: Copyright (c) 1991-2024 Unicode, Inc., under the Unicode\n"
             " * License v3 (LICENSES/Unicode-License-v3.txt). The code around it:\n"
             " * Copyright (c) 2026 AmigaChrome Team, MIT licence (see LICENSE). */\n"
             "#include \"of_unicode.h\"\n\n#include <stddef.h>\n\n" % UCD_VERSION)
    o.append("/* general category, combining class, default ignorable */\n")
    o.append("#define CO_RECORD %d\n" % rec_index[(CATEGORIES.index("Co"), 0, 0)])
    o.append(carray("records_gc", "uint8_t", [r[0] for r in records]))
    o.append(carray("records_ccc", "uint8_t", [r[1] for r in records]))
    o.append(carray("records_ign", "uint8_t", [r[2] for r in records]))
    o.append(carray("prop_stage1", "uint16_t" if len(propb) > 255 else "uint8_t", prop1))
    o.append(carray("prop_blocks", "uint8_t", [v for blk in propb for v in blk]))
    o.append(carray("script_stage1", "uint16_t" if len(scrb) > 255 else "uint8_t", scr1))
    o.append(carray("script_blocks", "uint8_t", [v for blk in scrb for v in blk]))
    o.append("/* canonical decompositions, one step, by codepoint */\n")
    o.append(carray("dec_cp", "uint32_t", [c for c, d in dec], 8))
    o.append(carray("dec_a", "uint32_t", [d[0] for c, d in dec], 8))
    o.append(carray("dec_b", "uint32_t", [d[1] if len(d) > 1 else 0 for c, d in dec], 8))
    o.append("/* primary composites, by their two parts */\n")
    o.append(carray("comp_a", "uint32_t", [x[0] for x in comp], 8))
    o.append(carray("comp_b", "uint32_t", [x[1] for x in comp], 8))
    o.append(carray("comp_c", "uint32_t", [x[2] for x in comp], 8))
    o.append("""/* the tables' row for cp, or -1 outside them */
static long row(uint32_t cp)
{
    if (cp < 0x40000)
        return (long)(cp / %d);
    if (cp >= 0xE0000 && cp < 0xE1000)
        return (long)((cp - 0xE0000) / %d + 0x40000 / %d);
    return -1;
}

static unsigned prop(uint32_t cp)
{
    long r = row(cp);
    if (r < 0)
        return (cp >= 0xF0000 && cp <= 0x10FFFD && (cp & 0xFFFF) <= 0xFFFD) ? CO_RECORD : 0;
    return prop_blocks[(uint32_t)prop_stage1[r] * %d + cp %% %d];
}

unsigned of_uni_category(uint32_t cp)
{
    return records_gc[prop(cp)];
}

unsigned of_uni_combining_class(uint32_t cp)
{
    return records_ccc[prop(cp)];
}

int of_uni_default_ignorable(uint32_t cp)
{
    return records_ign[prop(cp)];
}

unsigned of_uni_script(uint32_t cp)
{
    long r = row(cp);
    if (r < 0)
        return OF_SCRIPT_UNKNOWN;
    return script_blocks[(uint32_t)script_stage1[r] * %d + cp %% %d];
}

/* Hangul syllables decompose and compose by arithmetic (the Unicode
 * Standard, 3.12) */
#define S_BASE 0xAC00
#define L_BASE 0x1100
#define V_BASE 0x1161
#define T_BASE 0x11A7
#define L_COUNT 19
#define V_COUNT 21
#define T_COUNT 28
#define N_COUNT (V_COUNT * T_COUNT)
#define S_COUNT (L_COUNT * N_COUNT)

int of_uni_decompose(uint32_t cp, uint32_t *a, uint32_t *b)
{
    size_t lo = 0, hi = sizeof dec_cp / sizeof dec_cp[0];
    if (cp >= S_BASE && cp < S_BASE + S_COUNT) {
        uint32_t s = cp - S_BASE;
        if (s %% T_COUNT) {
            *a = cp - s %% T_COUNT;
            *b = T_BASE + s %% T_COUNT;
        } else {
            *a = L_BASE + s / N_COUNT;
            *b = V_BASE + (s %% N_COUNT) / T_COUNT;
        }
        return 2;
    }
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (dec_cp[mid] < cp)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < (size_t)(sizeof dec_cp / sizeof dec_cp[0]) && dec_cp[lo] == cp) {
        *a = dec_a[lo];
        *b = dec_b[lo];
        return dec_b[lo] ? 2 : 1;
    }
    return 0;
}

uint32_t of_uni_compose(uint32_t a, uint32_t b)
{
    size_t lo = 0, hi = sizeof comp_a / sizeof comp_a[0];
    if (a >= L_BASE && a < L_BASE + L_COUNT && b >= V_BASE && b < V_BASE + V_COUNT)
        return S_BASE + ((a - L_BASE) * V_COUNT + (b - V_BASE)) * T_COUNT;
    if (a >= S_BASE && a < S_BASE + S_COUNT && !((a - S_BASE) %% T_COUNT) &&
        b > T_BASE && b < T_BASE + T_COUNT)
        return a + (b - T_BASE);
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (comp_a[mid] < a || (comp_a[mid] == a && comp_b[mid] < b))
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < sizeof comp_a / sizeof comp_a[0] && comp_a[lo] == a && comp_b[lo] == b)
        return comp_c[lo];
    return 0;
}
""" % (BLOCK, BLOCK, BLOCK, BLOCK, BLOCK, BLOCK, BLOCK))
    with open(out, "w", encoding="utf-8") as f:
        f.write("".join(o))

    # the header's enumerations
    hdr = os.path.join(os.path.dirname(out), "of_unicode_enums.h")
    h = ["/* Generated by tools/gen_unicode.py from the UCD %s: do not edit.\n"
         " * Copyright (c) 2026 AmigaChrome Team. MIT licence (see LICENSE). */\n"
         "#ifndef OF_UNICODE_ENUMS_H\n#define OF_UNICODE_ENUMS_H\n\n" % UCD_VERSION]
    h.append("/* general categories */\nenum {\n")
    for i, c in enumerate(CATEGORIES):
        h.append("    OF_GC_%s = %d,\n" % (c.upper(), i))
    h.append("};\n\n/* scripts (Scripts.txt) */\nenum {\n")
    for i, s in enumerate(script_names):
        h.append("    OF_SCRIPT_%s = %d,\n" % (s.upper(), i))
    h.append("    OF_SCRIPT_COUNT = %d\n};\n\n#endif\n" % len(script_names))
    with open(hdr, "w", encoding="utf-8") as f:
        f.write("".join(h))
    size = len(records) * 3 + len(prop1) * (2 if len(propb) > 255 else 1) + len(propb) * BLOCK \
        + len(scr1) * (2 if len(scrb) > 255 else 1) + len(scrb) * BLOCK + len(dec) * 12 + len(comp) * 12
    print("%s: %d records, %d property blocks, %d script blocks, %d decompositions, %d composites, about %d bytes"
          % (out, len(records), len(propb), len(scrb), len(dec), len(comp), size))


if __name__ == "__main__":
    main()
