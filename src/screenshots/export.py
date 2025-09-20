
import sys
import json
import base64
import itertools

TO_EXPORT = [
        "Obstacle Run",
        "TRSDOS 1.3",
        "Scarfman",
        "Sea Dragon",
        "Defense Command",
]

# Always six chars.
def printable_char(ch):
    if ch >= 32 and ch < 128:
        return "'%c',  " % ch
    else:
        return "0x%02X, " % ch

def main():
    data = json.load(open("my-trs-80.json"))
    files = data["files"]
    for file in files:
        name = file["name"]
        if name in TO_EXPORT:
            screenshots = file["screenshots"]
            for index, screenshot in enumerate(screenshots):
                sys.stderr.write(name + " (" + str(index) + ")\n")

                # Remove version number.
                screenshot = screenshot[2:]

                # Decode.
                screenshot = base64.b64decode(screenshot)

                # Skip expanded mode (assume not).
                screenshot = screenshot[1:]

                # Accumulated bytes.
                screen = []

                i = 0
                while i < len(screenshot):
                    v = screenshot[i]
                    i += 1
                    if v > 32 and v < 128:
                        # Implicit count of 1.
                        screen += [v]
                        pass
                    else:
                        count = screenshot[i]
                        i += 1
                        screen += [v]*count

                print()
                print("// " + name + " " + str(index))
                print("uint8_t screen[] = {")
                for line in itertools.batched(screen, 64):
                    for span in itertools.batched(line, 8):
                        print("    " + "".join(printable_char(x) for x in span))
                    print()
                print("};")

main()
