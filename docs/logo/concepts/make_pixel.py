# Generate pixel.svg from a 16x16 character grid.
import sys

GRID = [
    "................",
    ".....KKKKKK.....",
    "...KKOOOOOOKK...",
    "..KOOLLOOOOOOK..",
    "..KOLLOOOOOOOK..",
    ".KOOOOOOOOOOOOK.",
    ".KOOWWOOOOWWOOK.",
    ".KOOWKOOOOWKOOK.",
    ".KOOOOOOOOOOOOK.",
    ".KDOOOOOOOOOODK.",
    "..KDDOOOOOODDK..",
    "..OOO.OOOO.OOO..",
    ".OO..OO..OO..OO.",
    "OO...O....O...OO",
    "O.D..OO..OO..D.O",
    ".DD...D..D...DD.",
]

COLORS = {
    "K": "#3A1A05",
    "O": "#F7922F",
    "D": "#C85A0C",
    "L": "#FFC27A",
    "W": "#FFFFFF",
}

def main(out_path):
    rects = []
    for y, row in enumerate(GRID):
        assert len(row) == 16, (y, len(row))
        for x, ch in enumerate(row):
            if ch in COLORS:
                rects.append('  <rect x="%d" y="%d" width="1" height="1" fill="%s"/>' % (x, y, COLORS[ch]))
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" shape-rendering="crispEdges">\n'
           + "\n".join(rects) + "\n</svg>\n")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(svg)

main(sys.argv[1])
