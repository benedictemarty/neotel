#!/usr/bin/env python3
# SPDX-License-Identifier: EUPL-1.2
"""gen_font80.py - genere src/font80.c : police 8x14 du mode 80 colonnes de NeoTel.

Source : police console Linux Lat15-VGA14.psf.gz (paquet console-setup : « All
console fonts are public domain by nature »), meme origine que font_8x14.h du
firmware Neo6502 (mode Hercules). Glyphes extraits :
  - 0x20-0x7F : jeu americain (ASCII) ;
  - FONT80_FR_BASE.. : les 10 caracteres du jeu francais NF Z 62-010 qui
    remplacent # @ [ \\ ] ^ ` { | } ~ (STUM 1B tableau 4 : £ à ° ç § ^ ` é ù è ¨) ;
  - FONT80_BLOCK : pave plein (symbole d'erreur, CAN/SUB).
Usage : tools/gen_font80.py [Lat15-VGA14.psf.gz] > src/font80.c
"""
import gzip, struct, sys

src = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/consolefonts/Lat15-VGA14.psf.gz"
data = gzip.open(src).read()
assert data[0:2] == b"\x36\x04", "PSF1 attendu"
mode, height = data[2], data[3]
assert height == 14
count = 512 if mode & 1 else 256
glyphs = [data[4 + i * height:4 + (i + 1) * height] for i in range(count)]
cp_to_glyph = {}
if mode & 2:
    p = 4 + count * height
    for g in range(count):
        while True:
            cp = struct.unpack_from("<H", data, p)[0]; p += 2
            if cp == 0xFFFF: break
            if cp == 0xFFFE: continue
            cp_to_glyph.setdefault(cp, g)
else:
    cp_to_glyph = {i: i for i in range(count)}

# jeu francais (STUM 1B, tableau 4, relu sur docs/ref/stum1b-img/p171.svg) :
# code ASCII remplace -> caractere
FR = [(0x23, '£', 'livre'), (0x40, 'à', 'a grave'), (0x5B, '°', 'degre'), (0x5C, 'ç', 'c cedille'),
      (0x5D, '§', 'section'), (0x5E, '^', 'circonflexe'), (0x60, '`', 'grave'), (0x7B, 'é', 'e aigu'),
      (0x7C, 'ù', 'u grave'), (0x7D, 'è', 'e grave'), (0x7E, None, 'trema')]
# Le trema seul n'est pas dans la police : deux points en haut de cellule.
TREMA = bytes([0x00, 0x00, 0x6C, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])

def glyph(cp):
    g = cp_to_glyph.get(cp)
    if g is None:
        sys.stderr.write("glyphe absent : U+%04X\n" % cp); g = cp_to_glyph[0x3F]
    return glyphs[g]

out = []
out.append("/* font80.c - police 8x14 du mode 80 colonnes (genere par tools/gen_font80.py")
out.append(" * depuis Lat15-VGA14.psf.gz, console-setup, domaine public). NE PAS EDITER. */")
out.append('#include "font80.h"')
out.append("")
out.append("const unsigned char font80[FONT80_COUNT * FONT80_H] = {")
rows = []
for ch in range(0x20, 0x80):
    rows.append((glyph(ch), "$%02X %s" % (ch, chr(ch) if 0x20 < ch < 0x7F and ch not in (0x5C, 0x2A, 0x2F) else "")))
for code, c, name in FR:
    rows.append((glyph(ord(c)) if c else TREMA, "FR $%02X %s" % (code, name)))
rows.append((bytes([0xFF] * 14), "pave plein (erreur)"))
for g, com in rows:
    out.append("    " + ",".join("0x%02X" % b for b in g) + ", /* " + com + " */")
out.append("};")
out.append("")
out.append("/* Code ASCII (0x20-0x7F) -> index dans font80, jeu francais : les 11")
out.append(" * positions de NF Z 62-010 renvoient aux glyphes FR. */")
out.append("const unsigned char font80_fr_index[96] = {")
idx = list(range(96))
for i, (code, c, name) in enumerate(FR):
    idx[code - 0x20] = 96 + i
out.append("    " + ",".join(str(v) for v in idx))
out.append("};")
out.append("")
out.append("/* Offset (octets) de chaque glyphe dans font80, pour l'assembleur */")
out.append("const unsigned int font80_offset[FONT80_COUNT] = {")
out.append("    " + ",".join(str(i * 14) for i in range(len(rows))))
out.append("};")
print("\n".join(out))
