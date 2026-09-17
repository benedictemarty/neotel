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

# Jeu complementaire (STUM 2 annexe 3.12 p. 92, relu sur le scan) : code -> caractere,
# None = identique a l'ASCII. Approximations : 6/1 (croix) -> +, 7/4-7/8 (traits) -> box.
COMP = {0x40: '@', 0x41: 'à', 0x42: 'â', 0x43: 'ä', 0x44: 'é', 0x45: 'è', 0x46: 'ê', 0x47: 'ë',
        0x48: 'î', 0x49: 'ï', 0x4A: 'ô', 0x4B: 'ö', 0x4C: 'ù', 0x4D: 'û', 0x4E: 'ü', 0x4F: '¼',
        0x50: '½', 0x51: '¾', 0x52: 'Ä', 0x53: 'É', 0x54: 'ì', 0x55: 'Ò', 0x56: 'Ü', 0x57: 'Ñ',
        0x58: 'ñ', 0x59: 'µ', 0x5A: '¿', 0x5B: '[', 0x5C: '\\', 0x5D: ']', 0x5E: '^', 0x5F: '─',
        0x60: '`', 0x61: '+', 0x62: '¤', 0x63: ' ', 0x64: '{', 0x65: '}', 0x66: '°', 0x67: '±',
        0x68: '÷', 0x69: ' ', 0x6A: '↑', 0x6B: '→', 0x6C: '↓', 0x6D: '←', 0x6E: '+', 0x6F: ' ',
        0x70: ' ', 0x71: ' ', 0x72: ' ', 0x73: ' ', 0x74: '│', 0x75: '│', 0x76: '─', 0x77: '─',
        0x78: '│', 0x79: 'Œ', 0x7A: 'œ', 0x7B: 'ß', 0x7C: '~', 0x7D: '£', 0x7E: '·'}
# Jeu DEC (STUM 2 annexe 3.13 p. 93 = jeu "special graphics" DEC). Les cinq
# traits de balayage 6/F-7/3 (scan 1, 3, 5, 7, 9 d'une cellule DEC de 10 lignes)
# sont des traits horizontaux a des hauteurs distinctes ; on les place dans la
# cellule 8x14 aux lignes 1, 4, 7, 10, 13 (scan 5 = trait median comme "─").
def hbar(row):
    return bytes([0xFF if r == row else 0x00 for r in range(14)])
DEC_RAW = {0x6F: hbar(1), 0x70: hbar(4), 0x71: hbar(7), 0x72: hbar(10), 0x73: hbar(13)}
DEC = {0x5F: ' ', 0x60: ' ', 0x61: ' ', 0x62: ' ', 0x63: ' ', 0x64: ' ', 0x65: ' ',
       0x66: '°', 0x67: '±', 0x68: ' ', 0x69: ' ', 0x6A: '┘', 0x6B: '┐', 0x6C: '┌', 0x6D: '└',
       0x6E: '┼', 0x74: '├', 0x75: '┤',
       0x76: '┴', 0x77: '┬', 0x78: '│', 0x79: '≤', 0x7A: '≥', 0x7B: ' ', 0x7C: '≠', 0x7D: '£',
       0x7E: '·'}

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
rows = []
for ch in range(0x20, 0x80):
    rows.append((glyph(ch), "$%02X %s" % (ch, chr(ch) if 0x20 < ch < 0x7F and ch not in (0x5C, 0x2A, 0x2F) else "")))
for code, c, name in FR:
    rows.append((glyph(ord(c)) if c else TREMA, "FR $%02X %s" % (code, name)))
rows.append((bytes([0xFF] * 14), "pave plein (erreur)"))
# glyphes supplementaires des jeux complementaire et DEC (dedoublonnes)
extra = {}
def extra_index(c):
    if c not in extra:
        extra[c] = len(rows)
        rows.append((glyph(ord(c)), "extra U+%04X" % ord(c)))
    return extra[c]
comp_idx = list(range(96)); dec_idx = list(range(96))
for code, c in COMP.items():
    comp_idx[code - 0x20] = 0 if c == ' ' else (code - 0x20 if c == chr(code) else extra_index(c))
for code, c in DEC.items():
    dec_idx[code - 0x20] = 0 if c == ' ' else (code - 0x20 if c == chr(code) else extra_index(c))
# Traits de balayage DEC : glyphes bruts (une ligne horizontale chacun)
raw_extra = {}
for code, g in DEC_RAW.items():
    key = bytes(g)
    if key not in raw_extra:
        raw_extra[key] = len(rows)
        rows.append((g, "scan DEC $%02X" % code))
    dec_idx[code - 0x20] = raw_extra[key]
out.append("const unsigned char font80[FONT80_COUNT * FONT80_H] = {")
for g, com in rows:
    out.append("    " + ",".join("0x%02X" % b for b in g) + ", /* " + com + " */")
out.append("};")
out.append("")
out.append("/* FONT80_COUNT attendu = %d (font80.h) */" % len(rows))
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
out.append("/* Jeu complementaire (STUM 2 annexe 3.12) : code ASCII -> index dans font80 */")
out.append("const unsigned char font80_comp_index[96] = {")
out.append("    " + ",".join(str(v) for v in comp_idx))
out.append("};")
out.append("")
out.append("/* Jeu DEC (STUM 2 annexe 3.13) : code ASCII -> index dans font80 */")
out.append("const unsigned char font80_dec_index[96] = {")
out.append("    " + ",".join(str(v) for v in dec_idx))
out.append("};")
out.append("")
out.append("/* Offset (octets) de chaque glyphe dans font80, pour l'assembleur */")
out.append("const unsigned int font80_offset[FONT80_COUNT] = {")
out.append("    " + ",".join(str(i * 14) for i in range(len(rows))))
out.append("};")
print("\n".join(out))
