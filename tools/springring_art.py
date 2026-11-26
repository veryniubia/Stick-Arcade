"""Build both harbor bitmaps from the hand-retouched, native pixel art."""
from pathlib import Path
from png2c import convert, emit

root = Path(__file__).resolve().parents[1]
w, h, palette, packed = convert(root / 'assets/springring/harbor.png', False)
assert (w, h) == (120, 56)
out = root / 'src/games/springring'
(out / 'harbor_card.h').write_bytes(emit('harborCard', w, h, palette, packed, False).encode())
# Gameplay uses the same art at exactly two screen pixels per art pixel.
expanded = bytearray()
for y in range(h):
    row = bytearray()
    for byte in packed[y*w//2:(y+1)*w//2]:
        row.extend(((byte >> 4)*17, (byte & 15)*17))
    expanded.extend(row*2)
(out / 'harbor.h').write_bytes(emit('harbor', w*2, h*2, palette, expanded, False).encode())

