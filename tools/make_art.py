#!/usr/bin/env python3
"""Generate the party artwork: HD gate sign (alt texture) and the title card (C header)."""
import zipfile, struct, sys, math
sys.path.insert(0, '/home/claude/tools'); import otex
from PIL import Image, ImageDraw, ImageFont, ImageFilter

POP = '/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf'
LORA = '/usr/share/fonts/truetype/google-fonts/Lora-Variable.ttf'
z = zipfile.ZipFile('/home/claude/sohtest/oot.o2r')
orig_b = z.read('scenes/shared/spot01_scene/spot01_sceneTex_012B50')
orig = otex.decode(orig_b)

# ---------------- gate sign: 256x128, same silhouette ----------------
W, H = 256, 128
mask = orig.split()[3].resize((W, H), Image.NEAREST)
sign = Image.new('RGBA', (W, H), (0, 0, 0, 0))
d = ImageDraw.Draw(sign)
# painted board: warm pink with soft vertical grain, gold border
for y in range(H):
    for x in range(W):
        g = 8 * math.sin(x * 0.07 + 3 * math.sin(y * 0.11)) + 4 * math.sin(y * 0.9)
        sign.putpixel((x, y), (int(248 + g * 0.3), int(188 + g), int(214 + g * 0.6), 255))
d.rounded_rectangle([5, 5, W - 6, H - 6], radius=10, outline=(196, 140, 40, 255), width=6)
d.rounded_rectangle([9, 9, W - 10, H - 10], radius=8, outline=(255, 222, 90, 255), width=2)

def outlined(draw, xy, text, font, fill, outline, ow):
    for dx in range(-ow, ow + 1):
        for dy in range(-ow, ow + 1):
            if dx * dx + dy * dy <= ow * ow + 1:
                draw.text((xy[0] + dx, xy[1] + dy), text, font=font, fill=outline)
    draw.text(xy, text, font=font, fill=fill)

f1 = ImageFont.truetype(POP, 30)
t = 'WELCOME'
tw = d.textlength(t, font=f1)
outlined(d, ((W - tw) / 2, 12), t, f1, (255, 255, 255, 255), (120, 40, 90, 255), 3)
# HARVIE in rainbow letters
f2 = ImageFont.truetype(POP, 52)
name = 'HARVIE'
rainbow = [(235, 60, 70), (255, 150, 30), (250, 210, 40), (70, 190, 90), (60, 140, 240), (160, 90, 230)]
total = sum(d.textlength(c, font=f2) for c in name) + 2 * (len(name) - 1)
x = (W - total) / 2
for i, c in enumerate(name):
    outlined(d, (x, 44), c, f2, rainbow[i] + (255,), (70, 20, 60, 255), 3)
    x += d.textlength(c, font=f2) + 2
# little hearts in the corners
def heart(cx, cy, s, col):
    d.ellipse([cx - s, cy - s, cx, cy], fill=col); d.ellipse([cx, cy - s, cx + s, cy], fill=col)
    d.polygon([(cx - s, cy - s / 2), (cx + s, cy - s / 2), (cx, cy + s)], fill=col)
for cx, cy in [(24, 26), (W - 24, 26), (24, H - 24), (W - 24, H - 24)]:
    heart(cx, cy, 7, (230, 50, 110, 255))
# respect the original silhouette
sign.putalpha(Image.eval(mask, lambda a: 255 if a >= 128 else 0))
# The gate sign is one double-sided quad whose UVs read correctly from inside the
# village. Mirror the art so it reads correctly from outside, where she arrives.
sign = sign.transpose(Image.FLIP_LEFT_RIGHT)
sign.save('/tmp/sign_new.png')

hdr = bytearray(orig_b[:0x40])
hdr[1] = 1                                   # custom asset
hdr[8:12] = struct.pack('<I', 1)             # resource version 1 (HD fields)
# HD replacement in libultraship's format: raw RGBA32 pixels, LOAD_AS_RAW flag,
# byte scale = new row bytes / original row bytes (256*4 / 64*2 = 8), pixel scale = 4
data = sign.convert('RGBA').tobytes()
TEX_FLAG_LOAD_AS_RAW = 1
res = bytes(hdr) + struct.pack('<IIIIffI', 1, W, H, TEX_FLAG_LOAD_AS_RAW, (W * 4) / (64 * 2), H / 32, len(data)) + data
open('/tmp/spot01_sceneTex_012B50', 'wb').write(res)

# ---------------- title card: IA8 144x24, the size of the game's own cards ----------------
# (a 256x32 card is accepted by the game but never drawn by the renderer)
TW, TH = 144, 24
card = Image.new('LA', (TW, TH), (0, 0))
cd = ImageDraw.Draw(card)
lf = ImageFont.truetype(LORA, 17)
try:
    lf.set_variation_by_name('Bold')
except Exception:
    pass
text = 'Welcome, Harvie!'
tw = cd.textlength(text, font=lf)
x0, y0 = (TW - tw) / 2, (TH - 17) / 2 - 2
for dx in range(-2, 3):
    for dy in range(-2, 3):
        if dx * dx + dy * dy <= 5:
            cd.text((x0 + dx, y0 + dy), text, font=lf, fill=(0, 255))
cd.text((x0, y0), text, font=lf, fill=(255, 255))
card.save('/tmp/card_new.png')
ia8 = bytes(((l >> 4) << 4) | (a >> 4) for l, a in card.convert('LA').getdata())
card_hdr = z.read('textures/g_pn_28/gKakarikoVillageTitleCardENGTex')[:0x40]
open('/tmp/gWelcomeHarvieCardTex', 'wb').write(card_hdr + struct.pack('<IIII', 8, TW, TH, len(ia8)) + ia8)

# ---------------- pack the mod archive ----------------
import json, shutil
src = '/home/claude/rg/mod/Zeldas Errands 0.1.o2r'
m = zipfile.ZipFile(src); out = src + '.new'
keep = {'README.txt', 'manifest.json'}
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as w:
    for n in m.namelist():
        if n in keep:
            w.writestr(n, m.read(n))
    w.write('/tmp/spot01_sceneTex_012B50', 'alt/scenes/shared/spot01_scene/spot01_sceneTex_012B50')
    w.write('/tmp/gWelcomeHarvieCardTex', 'textures/zeldas_errands/gWelcomeHarvieCardTex')
shutil.move(out, src)
print('packed', zipfile.ZipFile(src).namelist())
