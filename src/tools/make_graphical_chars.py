
# The fonts from xtrs don't include the graphical characters, so
# we procedurally generate those here.

for i in range(64):
    s = "    "
    k = i
    for y in range(3):
        b = 0

        if (k & 1) != 0:
            b |= 0x0F
        k >>= 1
        if (k & 1) != 0:
            b |= 0xF0
        k >>= 1

        s += "0x%02x," % b
        s += "0x%02x," % b
        s += "0x%02x," % b
        s += "0x%02x," % b

    print(s)


