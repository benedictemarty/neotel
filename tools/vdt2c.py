#!/usr/bin/env python3
# SPDX-License-Identifier: EUPL-1.2
"""vdt2c.py PAGE.vdt SORTIE.h NOM - tableau C d'une page Videotex (bancs cible)."""
import sys
data = open(sys.argv[1], "rb").read()
name = sys.argv[3]
with open(sys.argv[2], "w") as f:
    f.write("/* genere par tools/vdt2c.py depuis %s */\n" % sys.argv[1])
    f.write("#define %s_LEN %d\n" % (name.upper(), len(data)))
    f.write("static const unsigned char %s[%d] = {\n" % (name, len(data)))
    for i in range(0, len(data), 16):
        f.write("    " + ", ".join("0x%02X" % b for b in data[i:i+16]) + ",\n")
    f.write("};\n")
