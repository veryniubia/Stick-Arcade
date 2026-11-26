"""Bake Copper Mill's shared collision spline and indexed pixel artwork."""
from math import dist, hypot, ceil
from pathlib import Path
from random import Random
from PIL import Image, ImageDraw

DEST = Path(__file__).resolve().parents[1] / 'src/games/hwydrift'
START = (85, 113)
# Each cubic continues from the previous endpoint, in race order.
CURVES = [
    ((120, 113), (169, 113), (192, 113)),
    ((213, 113), (222, 103), (222, 84)),
    ((222, 62), (225, 36), (206, 33)),
    ((184, 28), (177, 37), (168, 53)),
    ((161, 68), (157, 81), (145, 81)),
    ((131, 81), (127, 67), (121, 51)),
    ((112, 30), (98, 33), (77, 33)),
    ((52, 33), (27, 32), (20, 48)),
    ((12, 68), (24, 78), (43, 79)),
    ((58, 80), (65, 82), (65, 95)),
    ((65, 107), (71, 113), START),
]
PALETTE = ['16272e','233a3b','365348','52745b','86a470','26343c','43505a','55616a',
           '72948c','b3c8ac','f5e8bd','b45538','ee9352','205467','397b84','78b8ac']


def course():
    points = [START]
    for c1, c2, end in CURVES:
        start = points[-1]
        n = ceil((dist(start, c1) + dist(c1, c2) + dist(c2, end)) / 4)
        for i in range(1, n + 1):
            t = i / n
            u = 1 - t
            points.append(tuple(u**3*a + 3*u*u*t*b + 3*u*t*t*c + t**3*d
                                for a,b,c,d in zip(start,c1,c2,end)))
    return points


def artwork(points):
    rng = Random(431)
    im = Image.new('P', (240, 135), 2)
    d = ImageDraw.Draw(im)
    # Printed model railway scenery: grass tufts, a millrace and a timber wharf.
    for _ in range(880):
        x, y = rng.randrange(240), rng.randrange(135)
        d.line((x,y,x+rng.randrange(1,4),y), fill=rng.choice([1,3,3]))
    d.polygon([(0,88),(32,90),(44,100),(48,114),(64,135),(0,135)], fill=8)
    d.polygon([(0,90),(31,92),(42,101),(46,115),(62,135),(0,135)], fill=13)
    for _ in range(110):
        x,y=rng.randrange(57),rng.randrange(90,135)
        if im.getpixel((x,y))==13:
            d.line((x,y,x+rng.randrange(2,6),y),fill=rng.choice([14,14,15]))
    for x,y in [(8,104),(24,112)]:
        d.rectangle((x,y,x+4,y+13),fill=0)
        d.rectangle((x,y,x+3,y+11),fill=12)
    for y in range(99,110,2):
        d.line((0,y,29,y+4),fill=11 if y%4==1 else 12)
    # Sawtooth mill roof, vents, and tanks inside the western infield.
    d.rectangle((51,48,100,66),fill=1)
    for x in (49,64,79):
        d.rectangle((x,46,x+14,62),fill=11)
        d.polygon([(x,46),(x+12,43),(x+12,54),(x,57)], fill=12)
        d.line((x,46,x+12,43),fill=10)
        for i in (2,6,10): d.line((x+i,48,x+i,53),fill=11)
        d.rectangle((x+2,59,x+5,60), fill=0)
    for x,y in [(101,54),(106,64)]:
        d.rectangle((x-4,y-2,x+4,y+3),fill=5)
        d.ellipse((x-4,y-5,x+4,y+1),fill=9,outline=8)
    for x,y in [(192,60),(201,70),(185,83),(201,91),(94,86),(83,76),(8,17),(230,125)]:
        d.ellipse((x-5,y-2,x+7,y+6),fill=1)
        d.ellipse((x-5,y-5,x+5,y+4),fill=3)
        d.line((x-3,y-3,x+1,y-4),fill=4)
    # Loading crane in the eastern yard: ballast, gantry, cable and hook.
    d.rectangle((179,89,191,94),fill=1)
    d.rectangle((178,86,190,91),fill=5)
    d.line((181,88,183,71,187,88),fill=12,width=2)
    d.line((182,73,198,62,200,63,183,77),fill=11,width=2)
    d.line((182,72,198,62),fill=12)
    d.line((198,64,198,73,196,73),fill=9)
    d.rectangle((181,72,185,76),fill=12)
    d.point((184,73),fill=13)

    # Scenery follows the expanded centreline; paint the road at world resolution.
    im=im.resize((600,338),Image.Resampling.NEAREST)
    d=ImageDraw.Draw(im)
    for _ in range(2700):
        x,y=rng.randrange(600),rng.randrange(338)
        if im.getpixel((x,y))==2:
            d.line((x,y,x+2,y-1),fill=rng.choice([1,3,3]))
    best=[21**2]*(600*338)
    at=0
    for (ax,ay),(bx,by) in zip(points,points[1:]):
        dx,dy=bx-ax,by-ay
        length=hypot(dx,dy)
        for y in range(max(0,int(min(ay,by))-21),min(338,int(max(ay,by))+22)):
            for x in range(max(0,int(min(ax,bx))-21),min(600,int(max(ax,bx))+22)):
                t=max(0,min(1,((x-ax)*dx+(y-ay)*dy)/length**2))
                r=(x-ax-t*dx)**2+(y-ay-t*dy)**2
                i=y*600+x
                if r>=best[i]: continue
                best[i]=r
                along=at+t*length
                if r<15.4**2:
                    color=6
                    if r<.65**2 and int(along)%28<8: color=8
                    elif ((x*1877+y*991+x*y)%139)==0: color=7
                elif r<17.4**2: color=10 if int(along/7)%2 else 11
                elif r<18.5**2: color=5
                elif r<19.4**2: color=8
                else: color=1
                im.putpixel((x,y),color)
        at+=length
    x0,y0=map(round,points[0])
    for y in range(y0-15,y0+16):
        for x in range(x0-2,x0+4):
            d.point((x,y), fill=10 if ((x-x0+2)//3+(y-y0+15)//3)%2 else 5)
    for lane in (-7,7):
        d.line((x0-15,y0+lane-3,x0-15,y0+lane+3,x0-9,y0+lane+3),fill=9)
    for x in (x0+33,x0+39):
        d.line((x,y0-4,x+4,y0,x,y0+4),fill=9,width=2)
    d.rectangle((x0-2,y0-31,x0,y0-20),fill=5)
    d.polygon([(x0-2,y0-32),(x0+10,y0-32),(x0+6,y0-27),(x0-2,y0-27)],fill=12)
    return im


def table(name, data):
    rows=[','.join(f'0x{x:02x}' for x in data[i:i+24]) for i in range(0,len(data),24)]
    return 'constexpr uint8_t '+name+'[] = {\n'+',\n'.join(rows)+'\n};\n'


def packed(im):
    p=im.tobytes()
    return [(a<<4)|b for a,b in zip(p[::2],p[1::2])]


def main():
    points=[(x*2.5,y*2.5) for x,y in course()]
    (DEST/'course_data.inc').write_text('// Generated by tools/hwydrift_assets.py\n' +
        '\n'.join(f'    {{{x:.3f}f, {y:.3f}f}},' for x,y in points)+'\n')
    scene=artwork(points)
    colors=[tuple(bytes.fromhex(color)) for color in PALETTE]
    palette=', '.join(f'0x{((r>>3)<<11)|((g>>2)<<5)|(b>>3):04x}' for r,g,b in colors)
    (DEST/'sprite_data.inc').write_text('// Generated by tools/hwydrift_assets.py\n' +
        'constexpr uint16_t palette[] = {'+palette+'};\n'+
        table('mapPixels',packed(scene)))


if __name__=='__main__':
    main()

