#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
mkneo.py — Emballe un binaire brut dans le format exécutable .neo du Neo6502.

Format (cf. basic/scripts/makeexec.py du firmware) :
  $03 'N' 'E' 'O'  | version min (2 octets, 0) | adresse d'exécution (2)
  puis des blocs : contrôle (bit 7 = un bloc suit) | charge (2) | taille (2)
  | commentaire ASCIIZ | données.

Usage : mkneo.py entree.bin sortie.neo [charge_hex] [exec_hex] [commentaire]
"""

import sys


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    load = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x0800
    exec_ = int(sys.argv[4], 16) if len(sys.argv) > 4 else load
    comment = sys.argv[5] if len(sys.argv) > 5 else "AsteroNeo"
    data = open(src, "rb").read()
    out = bytearray([0x03, ord('N'), ord('E'), ord('O'), 0, 0, exec_ & 0xFF, exec_ >> 8])
    out += bytes([0x00, load & 0xFF, load >> 8, len(data) & 0xFF, len(data) >> 8])
    out += comment.encode("ascii") + b"\0"
    out += data
    open(dst, "wb").write(out)
    print(f"{dst} : {len(data)} octets à ${load:04X}, exec ${exec_:04X}")


if __name__ == "__main__":
    main()
