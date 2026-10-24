#!/usr/bin/env python3
"""Draws Twisted Breaks' MPC skin: the control-room look of the Mac plugin, for the MPC's 1280x628 plugin area.

    python3 skin/make_skin.py          # -> vst/art/*.png, vst/layout.conf, skin/preview/*.png

A Soviet-era nuclear plant control console left running for forty years (Source/UI/Palette.h in Twisted
Breaks): oxidised blue-green enamel over steel, chipped to red primer and bare metal at the edges, rust blooms
and drips under the rivets, yellowed bakelite label plates, black bakelite knobs, amber lamp keys, orange VFD
windows. Russian only in the header strip; every control label is English.

How it's made (the toolkit's "realistic hardware skin" route, after its Model-72 port): each tab is drawn at 2x,
then every control is cut from that tab with the control drawn in each of its positions, so a frame sits
exactly over its place: knobs and rotary switches are filmstrips, on/off keys image pairs, option key banks one
picture per option under clear touch segments. MPC draws the live text (values, the kit and pad names, the
status line) in its own font, in the VFD windows drawn here. The layout lines are written from the same
coordinates, so the pictures and the touch areas can't drift apart.

Deterministic (fixed seeds): running it twice gives the same files.
Needs Pillow and numpy.
"""
import math
import os
import random

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
VST = os.path.join(ROOT, "vst")
ART = os.path.join(VST, "art")
PREVIEW = os.path.join(HERE, "preview")
FONT_BOLD = os.path.join(HERE, "fonts", "DejaVuSansCondensed-Bold.ttf")
FONT_REG = os.path.join(HERE, "fonts", "DejaVuSansCondensed.ttf")

W, H = 1280, 628         # the plugin area
SS = 2                   # drawn at 2x, scaled down
Y_OFF = 86               # layout y = plugin y + 86 (the toolkit's Force Shadow coordinates)
FRAMES = 64              # knob filmstrip frames

# ---- Palette (Twisted Breaks Source/UI/Palette.h) ------------------------------------------------
STEEL_DARK = (28, 37, 35)
STEEL = (47, 61, 57)
STEEL_LIGHT = (66, 82, 76)
STEEL_EDGE = (93, 108, 100)
BARE = (141, 143, 134)
PRIMER = (110, 52, 36)
GRIME = (14, 18, 17)
RUST_DARK = (74, 36, 18)
RUST = (124, 61, 26)
RUST_BRIGHT = (165, 90, 36)
BAKELITE = (207, 195, 160)
BAKELITE_OLD = (169, 154, 116)
ENGRAVE = (27, 26, 22)
PLATE_BLACK = (20, 20, 19)
PLATE_TEXT = (217, 210, 189)
AMBER = (255, 169, 40)
AMBER_DIM = (74, 51, 23)
RED = (255, 58, 28)
VFD = (255, 143, 46)
HAZARD = (217, 164, 0)
PAPER = (216, 203, 152)
INK = (217, 210, 189)
INK_DIM = (150, 146, 128)

VFD_HEX = "ff8f2e"


def font(size, bold=True):
    return ImageFont.truetype(FONT_BOLD if bold else FONT_REG, int(round(size * SS)))


# =================================================================================================
#  Textures
# =================================================================================================
def smooth_noise(w, h, cell, rng):
    """Value noise in -1..1: random values on a grid `cell` px apart, smoothly scaled up."""
    gw, gh = max(2, w // cell + 2), max(2, h // cell + 2)
    g = Image.fromarray(((rng.random((gh, gw)) * 255).astype(np.uint8)), "L")
    g = g.resize((gw * cell, gh * cell), Image.BICUBIC).crop((0, 0, w, h))
    return np.asarray(g, dtype=np.float32) / 127.5 - 1.0


def enamel(w, h, seed, base=STEEL, grime=0.35):
    """Oxidised enamel: a mottled base, fine grain, a few scratches, grime settling toward the bottom."""
    rng = np.random.default_rng(seed)
    low = smooth_noise(w, h, 90 * SS, rng)
    mid = smooth_noise(w, h, 18 * SS, rng)
    fine = rng.normal(0, 1, (h, w)).astype(np.float32)
    shade = 1.0 + 0.10 * low + 0.05 * mid + 0.025 * fine
    ys = np.linspace(0, 1, h, dtype=np.float32)[:, None]
    shade -= grime * 0.18 * ys ** 2
    img = np.empty((h, w, 3), np.float32)
    for c in range(3):
        img[..., c] = base[c] * shade
    # oxidation: a greener, lighter bloom where the low noise is high
    ox = np.clip((low - 0.35) * 2.0, 0, 1)[..., None]
    img = img * (1 - 0.25 * ox) + np.array([78, 102, 88], np.float32) * 0.25 * ox
    im = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB")
    # scratches
    d = ImageDraw.Draw(im)
    r = random.Random(seed)
    for _ in range(int(w * h / (260000 * SS * SS)) + 3):
        x0, y0 = r.uniform(0, w), r.uniform(0, h)
        ang, ln = r.uniform(-0.5, 0.5) + (math.pi if r.random() < 0.5 else 0), r.uniform(20, 90) * SS
        x1, y1 = x0 + math.cos(ang) * ln, y0 + math.sin(ang) * ln
        col = tuple(min(255, int(v * 1.35)) for v in base)
        d.line((x0, y0, x1, y1), fill=col, width=1)
    return im


def chipped_edges(im, box, seed, band=9):
    """Wear at a panel's edge: chips through the enamel to red primer, bare steel in the middle of the bigger ones."""
    x, y, w, h = box
    rng = np.random.default_rng(seed)
    n1 = smooth_noise(w, h, 7 * SS, rng)
    n2 = smooth_noise(w, h, 3 * SS, rng)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    dist = np.minimum(np.minimum(xx, w - 1 - xx), np.minimum(yy, h - 1 - yy))
    reach = band * SS * (0.25 + 0.75 * np.clip(n1 * 0.5 + 0.5, 0, 1))
    chip = (dist < reach) & (n2 > 0.15)
    bare = chip & (dist < reach * 0.45) & (n2 > 0.45)
    full = np.asarray(im.crop((x, y, x + w, y + h)).convert("RGBA"), np.float32).copy()
    arr = full[..., :3]
    arr[chip] = arr[chip] * 0.15 + np.array(PRIMER, np.float32) * 0.85
    arr[bare] = np.array(BARE, np.float32) * (0.85 + 0.15 * n1[bare, None])
    # only inside the panel's rounded outline
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, w - 1, h - 1), radius=6 * SS, fill=255)
    patch = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGB")
    im.paste(patch, (x, y), m)


def rust(im, cx, cy, size, seed, drip=True):
    """A rust bloom at (cx, cy) (2x px), and drips running down from it."""
    r = random.Random(seed)
    rng = np.random.default_rng(seed)
    s = int(size * 2.6) | 1
    n = smooth_noise(s, s, max(3, size // 4), rng)
    yy, xx = np.mgrid[0:s, 0:s].astype(np.float32)
    d = np.hypot(xx - s / 2, yy - s / 2) / (s / 2)
    a = np.clip((1.0 - d * 1.15) + 0.55 * n, 0, 1) ** 1.6
    tone = np.clip(n * 0.5 + 0.5, 0, 1)[..., None]
    col = np.array(RUST_DARK, np.float32) * (1 - tone) + np.array(RUST_BRIGHT, np.float32) * tone
    layer = np.dstack([col, (a * 220)[..., None]]).astype(np.uint8)
    blob = Image.fromarray(layer, "RGBA").filter(ImageFilter.GaussianBlur(0.6 * SS))
    im.alpha_composite(blob, (int(cx - s / 2), int(cy - s / 2))) if im.mode == "RGBA" else im.paste(blob, (int(cx - s / 2), int(cy - s / 2)), blob)
    if drip:
        for _ in range(r.randint(1, 3)):
            dx = r.uniform(-size * 0.5, size * 0.5)
            ln = r.uniform(18, 70) * SS
            wd = r.uniform(1.0, 2.6) * SS
            dl = Image.new("RGBA", (int(wd * 4) + 4, int(ln) + 4), (0, 0, 0, 0))
            dd = ImageDraw.Draw(dl)
            steps = 24
            for k in range(steps):
                t0, t1 = k / steps, (k + 1) / steps
                alpha = int(170 * (1 - t0) ** 1.3)
                c = RUST if k % 3 else RUST_DARK
                dd.line((dl.width / 2, t0 * ln, dl.width / 2 + math.sin(t1 * 3) * 1.5, t1 * ln),
                        fill=c + (alpha,), width=max(1, int(wd * (1 - 0.5 * t0))))
            dl = dl.filter(ImageFilter.GaussianBlur(0.5 * SS))
            pos = (int(cx + dx - dl.width / 2), int(cy + size * 0.2))
            if im.mode == "RGBA":
                im.alpha_composite(dl, pos)
            else:
                im.paste(dl, pos, dl)


def speckle(im, box, seed, density=1.0):
    """Rust specks and pits scattered over a panel, thicker toward its edges, plus runs from the top edge."""
    x, y, w, h = box
    r = random.Random(seed)
    lay = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    n = int(w * h / (900 * SS * SS) * density)
    for _ in range(n):
        px, py = r.uniform(0, w), r.uniform(0, h)
        edge = min(px, w - px, py, h - py) / (40 * SS)
        if r.random() < 0.55 * min(1.0, edge):
            continue
        rad = r.choice((0.6, 0.8, 1.0, 1.3, 1.8)) * SS
        c = r.choice((RUST_DARK, RUST, RUST_BRIGHT, PRIMER))
        d.ellipse((px - rad, py - rad, px + rad, py + rad), fill=c + (r.randint(90, 220),))
    for _ in range(r.randint(2, 5)):   # streaks running down from the top edge
        px = r.uniform(20 * SS, w - 20 * SS)
        ln = r.uniform(15, 70) * SS
        for k in range(30):
            t = k / 30
            d.line((px + math.sin(t * 5) * SS, 6 * SS + t * ln, px + math.sin((t + 0.03) * 5) * SS, 6 * SS + (t + 0.034) * ln),
                   fill=RUST + (int(110 * (1 - t)),), width=int(1.4 * SS))
    lay = lay.filter(ImageFilter.GaussianBlur(0.35 * SS))
    im.alpha_composite(lay, (int(x), int(y)))


def soft_shadow(im, box, radius, blur, alpha, off=(3, 5), ellipse=False):
    x0, y0, x1, y1 = box
    pad = blur * 3
    lay = Image.new("L", (int(x1 - x0 + 2 * pad), int(y1 - y0 + 2 * pad)), 0)
    dl = ImageDraw.Draw(lay)
    rect = (pad, pad, pad + x1 - x0, pad + y1 - y0)
    if ellipse:
        dl.ellipse(rect, fill=alpha)
    else:
        dl.rounded_rectangle(rect, radius=radius, fill=alpha)
    lay = lay.filter(ImageFilter.GaussianBlur(blur))
    black = Image.new("RGBA", lay.size, (0, 0, 0, 255))
    black.putalpha(lay)
    im.alpha_composite(black, (int(x0 - pad + off[0] * SS), int(y0 - pad + off[1] * SS)))


def rivet(im, x, y, r, seed):
    """A domed rivet, lit from the top left, with a little rust round it."""
    rr = random.Random(seed)
    if rr.random() < 0.6:
        rust(im, x, y + r * 0.6, int(r * 1.6), seed, drip=rr.random() < 0.7)
    soft_shadow(im, (x - r, y - r, x + r, y + r), r, 2 * SS, 170, off=(1, 1.5), ellipse=True)
    d = ImageDraw.Draw(im)
    d.ellipse((x - r, y - r, x + r, y + r), fill=STEEL_DARK)
    for k in range(r, 0, -1):
        t = k / r
        c = tuple(int(STEEL_EDGE[i] * (0.55 + 0.45 * (1 - t)) + 10) for i in range(3))
        ox, oy = -(1 - t) * r * 0.35, -(1 - t) * r * 0.35
        d.ellipse((x + ox - k, y + oy - k, x + ox + k, y + oy + k), fill=c)
    d.ellipse((x - r * 0.45, y - r * 0.55, x - r * 0.05, y - r * 0.15), fill=(185, 190, 180))


# =================================================================================================
#  A tab
# =================================================================================================
class Tab:
    def __init__(self, name, qlinks, seed):
        self.name = name
        self.qlinks = qlinks
        self.seed = seed
        self.controls = []
        self.hi = enamel(W * SS, H * SS, seed, base=STEEL_DARK, grime=0.6).convert("RGBA")
        self.draw = ImageDraw.Draw(self.hi)
        self.header()

    # ---- primitives (layout px in, 2x px out) ---------------------------------------------------
    def text(self, x, y, s, size=15, bold=True, fill=INK, anchor="mm", spacing=0.0, shadow=True):
        f = font(size, bold)
        if shadow:
            self.draw.text((x * SS + SS, y * SS + SS), s, font=f, fill=(0, 0, 0, 160), anchor=anchor)
        self.draw.text((x * SS, y * SS), s, font=f, fill=fill, anchor=anchor)

    def engraved(self, x, y, s, size, fill=ENGRAVE, light=(255, 255, 240, 90)):
        f = font(size)
        self.draw.text((x * SS, y * SS + SS), s, font=f, fill=light, anchor="mm")
        self.draw.text((x * SS, y * SS), s, font=f, fill=fill, anchor="mm")

    # ---- the header strip: name plate, Russian subtitle, warning, maker's plate ------------------
    def header(self):
        hi, d = self.hi, self.draw
        strip = enamel(W * SS, 54 * SS, self.seed + 1, base=STEEL, grime=0.2)
        hi.paste(strip, (0, 0))
        d.line((0, 54 * SS, W * SS, 54 * SS), fill=GRIME, width=2 * SS)
        d.line((0, 54 * SS + 2 * SS, W * SS, 54 * SS + 2 * SS), fill=STEEL_EDGE + (120,), width=SS)
        # the name plate: black, raised cream letters
        bx = (12, 7, 560, 48)
        soft_shadow(hi, tuple(v * SS for v in bx), 4 * SS, 3 * SS, 190)
        d.rounded_rectangle(tuple(v * SS for v in bx), radius=4 * SS, fill=PLATE_BLACK, outline=(50, 52, 48), width=SS)
        f = font(27)
        d.text((26 * SS + 2 * SS, 28 * SS + 2 * SS), "TWISTED BREAKS", font=f, fill=(0, 0, 0), anchor="lm")
        d.text((26 * SS, 28 * SS), "TWISTED BREAKS", font=f, fill=PLATE_TEXT, anchor="lm")
        d.text((548 * SS, 28 * SS), "ПУЛЬТ УПРАВЛЕНИЯ БРЕЙКАМИ · ТБ-1М", font=font(11, False), fill=INK_DIM, anchor="rm")
        for sx in (18, 554):
            rivet(hi, sx * SS, 13 * SS, 3 * SS, self.seed + sx)
        # the warning, stencilled in amber
        f = font(22)
        warn = "ОСТОРОЖНО"
        glow = Image.new("RGBA", hi.size, (0, 0, 0, 0))
        ImageDraw.Draw(glow).text((735 * SS, 28 * SS), warn, font=f, fill=AMBER + (110,), anchor="mm")
        hi.alpha_composite(glow.filter(ImageFilter.GaussianBlur(5 * SS)))
        d.text((735 * SS, 28 * SS), warn, font=f, fill=(222, 170, 52), anchor="mm")
        # the maker's plate, bakelite
        px0, py0, px1, py1 = 905, 7, 1205, 48
        self.plate_box(px0, py0, px1, py1)
        self.engraved((px0 + px1) / 2, 21, "TWISTED GENETICS", 15)
        self.engraved((px0 + px1) / 2, 37, "ЗАВОД № 4 · ЗАВ. № 0001 · 1979", 10)
        # the radiation sign
        self.trefoil(1238, 28, 19)
        rust(hi, 880 * SS, 50 * SS, 9 * SS, self.seed + 77)

    def plate_box(self, x0, y0, x1, y1, screws=True):
        hi = self.hi
        box = tuple(v * SS for v in (x0, y0, x1, y1))
        soft_shadow(hi, box, 3 * SS, 2 * SS, 200, off=(2, 3))
        w, h = (x1 - x0) * SS, (y1 - y0) * SS
        face = enamel(int(w), int(h), int(x0 * 7 + y0), base=BAKELITE, grime=0.5)
        m = Image.new("L", face.size, 0)
        ImageDraw.Draw(m).rounded_rectangle((0, 0, w - 1, h - 1), radius=3 * SS, fill=255)
        hi.paste(face, (int(box[0]), int(box[1])), m)
        self.draw.rounded_rectangle(box, radius=3 * SS, outline=BAKELITE_OLD, width=SS)
        self.draw.line((box[0] + 2 * SS, box[3] - SS, box[2] - 2 * SS, box[3] - SS), fill=(120, 108, 80), width=SS)
        if screws:
            for sx in (x0 + 7, x1 - 7):
                cy = (y0 + y1) / 2
                self.draw.ellipse(((sx - 3) * SS, (cy - 3) * SS, (sx + 3) * SS, (cy + 3) * SS), fill=(110, 104, 88))
                self.draw.line(((sx - 2.4) * SS, cy * SS, (sx + 2.4) * SS, cy * SS), fill=(60, 56, 46), width=SS)

    def trefoil(self, cx, cy, r):
        d = self.draw
        pts = [((cx + r * math.sin(a)) * SS, (cy - r * math.cos(a)) * SS) for a in (0, 2 * math.pi / 3, 4 * math.pi / 3)]
        tri = [((cx) * SS, (cy - r) * SS), ((cx + r * 0.95) * SS, (cy + r * 0.7) * SS), ((cx - r * 0.95) * SS, (cy + r * 0.7) * SS)]
        d.polygon(tri, fill=HAZARD, outline=(40, 30, 0))
        c = (cx * SS, (cy + r * 0.12) * SS)
        rr = r * 0.52 * SS
        for k in range(3):
            a0 = -90 + 120 * k - 30
            d.pieslice((c[0] - rr, c[1] - rr, c[0] + rr, c[1] + rr), a0, a0 + 60, fill=(20, 18, 10))
        d.ellipse((c[0] - rr * 0.32, c[1] - rr * 0.32, c[0] + rr * 0.32, c[1] + rr * 0.32), fill=HAZARD)
        d.ellipse((c[0] - rr * 0.2, c[1] - rr * 0.2, c[0] + rr * 0.2, c[1] + rr * 0.2), fill=(20, 18, 10))
        del pts

    # ---- a panel: raised enamel, rivets, wear, a bakelite title plate ----------------------------
    def panel(self, x, y, w, h, title):
        hi = self.hi
        box = (x * SS, y * SS, (x + w) * SS, (y + h) * SS)
        soft_shadow(hi, box, 6 * SS, 4 * SS, 210, off=(2, 4))
        face = enamel(w * SS, h * SS, self.seed * 31 + x * 7 + y, base=STEEL, grime=0.3)
        m = Image.new("L", face.size, 0)
        ImageDraw.Draw(m).rounded_rectangle((0, 0, w * SS - 1, h * SS - 1), radius=6 * SS, fill=255)
        hi.paste(face, (x * SS, y * SS), m)
        chipped_edges(hi, (x * SS, y * SS, w * SS, h * SS), self.seed + x + y)
        d = self.draw
        d.rounded_rectangle(box, radius=6 * SS, outline=GRIME, width=SS)
        d.line((box[0] + 6 * SS, box[1] + SS, box[2] - 6 * SS, box[1] + SS), fill=STEEL_EDGE, width=SS)
        d.line((box[0] + 6 * SS, box[3] - 2 * SS, box[2] - 6 * SS, box[3] - 2 * SS), fill=GRIME, width=SS)
        r = random.Random(self.seed * 13 + x + y)
        for (rx, ry) in ((x + 11, y + 11), (x + w - 11, y + 11), (x + 11, y + h - 11), (x + w - 11, y + h - 11)):
            rivet(hi, rx * SS, ry * SS, 4 * SS, r.randint(0, 1 << 30))
        speckle(hi, (x * SS, y * SS, w * SS, h * SS), self.seed * 7 + x * 3 + y)
        for _ in range(max(1, w * h // 90000)):
            rust(hi, r.uniform(x + 20, x + w - 20) * SS, r.uniform(y + 30, y + h - 30) * SS, r.randint(5, 13) * SS,
                 r.randint(0, 1 << 30), drip=r.random() < 0.6)
        if title:
            f = font(13)
            tw = f.getlength(title) / SS
            self.plate_box(x + 24, y + 10, x + 24 + tw + 34, y + 32)
            self.engraved(x + 24 + (tw + 34) / 2, y + 21, title, 13)

    # ---- control decorations drawn into the tab ----------------------------------------------------
    def caption(self, cx, y, s, size=13):
        self.text(cx, y, s, size=size, fill=INK)

    def vfd_window(self, cx, cy, w, h):
        hi = self.hi
        box = ((cx - w / 2) * SS, (cy - h / 2) * SS, (cx + w / 2) * SS, (cy + h / 2) * SS)
        d = self.draw
        d.rounded_rectangle((box[0] - 3 * SS, box[1] - 3 * SS, box[2] + 3 * SS, box[3] + 3 * SS), radius=4 * SS, fill=(10, 12, 11))
        d.rounded_rectangle(box, radius=3 * SS, fill=(12, 8, 6))
        # a faint ghost of unlit segments and the glass's sheen
        g = Image.new("RGBA", hi.size, (0, 0, 0, 0))
        dg = ImageDraw.Draw(g)
        dg.rounded_rectangle(box, radius=3 * SS, fill=VFD + (10,))
        dg.rectangle((box[0] + 2 * SS, box[1] + 2 * SS, box[2] - 2 * SS, box[1] + (box[3] - box[1]) * 0.35), fill=(255, 255, 255, 9))
        hi.alpha_composite(g)
        d.line((box[0], box[3] + 3 * SS, box[2], box[3] + 3 * SS), fill=STEEL_EDGE + (90,), width=SS)

    def ticks(self, cx, cy, r, n=11, inner=6, outer=12):
        d = self.draw
        for i in range(n):
            a = math.radians(-135 + 270 * i / (n - 1))
            big = i in (0, n // 2, n - 1)
            r0, r1 = r + inner, r + outer + (3 if big else 0)
            d.line(((cx + r0 * math.sin(a)) * SS, (cy - r0 * math.cos(a)) * SS,
                    (cx + r1 * math.sin(a)) * SS, (cy - r1 * math.cos(a)) * SS),
                   fill=INK if big else INK_DIM, width=2 * SS if big else SS)

    # ---- controls -------------------------------------------------------------------------------------
    def knob(self, key, cx, cy, r, name, bw=112, value=True):
        """A black bakelite knob on a tick ring: name above, the value in a VFD window below (MPC's text)."""
        self.caption(cx, cy - r - 24, name)
        self.ticks(cx, cy, r)
        if value:
            self.vfd_window(cx, cy + r + 2 + 15, bw - 8, 26)
        soft_shadow(self.hi, ((cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS), r * SS, 5 * SS, 200, off=(3, 6), ellipse=True)
        self.controls.append(dict(kind="knob", key=key, cx=cx, cy=cy, r=r, bw=bw, value=value, n=FRAMES))

    def selector(self, key, cx, cy, r, labels, name):
        """A rotary switch: its positions engraved round it, the name below."""
        n = len(labels)
        d = self.draw
        f = font(11.5)
        for i, lab in enumerate(labels):
            deg = -120 + 240 * i / (n - 1)
            a = math.radians(deg)
            r0, r1 = r + 5, r + 11
            d.line(((cx + r0 * math.sin(a)) * SS, (cy - r0 * math.cos(a)) * SS,
                    (cx + r1 * math.sin(a)) * SS, (cy - r1 * math.cos(a)) * SS), fill=INK, width=2 * SS)
            rl = r + 15
            tx, ty = cx + rl * math.sin(a), cy - rl * math.cos(a)
            anchor = "mb" if abs(deg) < 30 else ("lm" if deg > 0 else "rm")
            if abs(deg) > 150:
                anchor = "mt"
            d.text((tx * SS + SS, ty * SS + SS), lab, font=f, fill=(0, 0, 0, 150), anchor=anchor)
            d.text((tx * SS, ty * SS), lab, font=f, fill=INK, anchor=anchor)
        self.caption(cx, cy + r + 26, name)
        soft_shadow(self.hi, ((cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS), r * SS, 5 * SS, 200, off=(3, 6), ellipse=True)
        self.controls.append(dict(kind="knob", key=key, cx=cx, cy=cy, r=r, bw=2 * r + 10, value=False, n=n,
                                  degs=[-120 + 240 * i / (n - 1) for i in range(n)], pointer=True))

    def toggle(self, key, cx, cy, label, w=118, h=58, name=None):
        if name:
            self.caption(cx, cy - h / 2 - 14, name)
        self.controls.append(dict(kind="pair", key=key, cx=cx, cy=cy, w=w, h=h, label=label))

    def button(self, key, cx, cy, label, w=150, h=58, name=None):
        if name:
            self.caption(cx, cy - h / 2 - 14, name)
        self.controls.append(dict(kind="button", key=key, cx=cx, cy=cy, w=w, h=h, label=label))

    def keys(self, key, cx, cy, labels, sw, sh, name=None, rows=1, label_size=13):
        """A bank of lamp keys, one lit: one picture an option under clear segments (the toolkit's enum_h)."""
        rects = seg_rects(cx, cy, len(labels), sw, sh, rows)
        if name:
            top = min(r[1] for r in rects)
            self.caption(cx, top - 14, name)
        self.controls.append(dict(kind="keys", key=key, cx=cx, cy=cy, labels=labels, sw=sw, sh=sh, rows=rows,
                                  rects=rects, label_size=label_size))

    def readout(self, key, cx, cy, w, h, tsize=24, name=None):
        if name:
            self.caption(cx, cy - h / 2 - 16, name)
        self.vfd_window(cx, cy, w, h)
        self.controls.append(dict(kind="readout", key=key, cx=cx, cy=cy, w=w, h=h, tsize=tsize))

    def tag(self, cx, cy, lines, w, h, angle=-2.0):
        """A yellowed paper tag taped on (instructions)."""
        t = Image.new("RGBA", (int(w * SS), int(h * SS)), PAPER + (255,))
        tn = enamel(int(w * SS), int(h * SS), int(cx + cy), base=PAPER, grime=0.4).convert("RGBA")
        t = tn
        dt = ImageDraw.Draw(t)
        f = font(12, bold=False)
        for i, s in enumerate(lines):
            dt.text((10 * SS, (18 + i * 16) * SS), s, font=f, fill=(40, 36, 28), anchor="lm")
        dt.rectangle((int(w * SS / 2 - 22 * SS), 0, int(w * SS / 2 + 22 * SS), 7 * SS), fill=(190, 186, 160, 200))
        t = t.rotate(angle, resample=Image.BICUBIC, expand=True)
        soft_shadow(self.hi, ((cx - w / 2) * SS, (cy - h / 2) * SS, (cx + w / 2) * SS, (cy + h / 2) * SS), 2 * SS, 3 * SS, 140, off=(2, 3))
        self.hi.alpha_composite(t, (int((cx * SS) - t.width / 2), int((cy * SS) - t.height / 2)))


def seg_rects(cx, cy, n, sw, sh, rows=1, gap=2):
    """The toolkit's enum_h segment rectangles (tools/shadow_skin.py seg_rects), in plugin-area px."""
    per = -(-n // rows)
    out = []
    for i in range(n):
        r, c = divmod(i, per)
        cnt = min(per, n - r * per)
        x0 = cx - (cnt * sw + (cnt - 1) * gap) // 2
        out.append((x0 + c * (sw + gap), cy - sh // 2 + r * (sh + gap), sw, sh))
    return out


# =================================================================================================
#  Sprites
# =================================================================================================
def knob_body(d):
    """A black bakelite knob d px across (2x): knurled skirt and domed cap, lit from the top left. No pointer."""
    s = int(d * 1.08) | 1
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    c = s / 2
    r = d / 2
    dr = ImageDraw.Draw(im)
    dr.ellipse((c - r, c - r, c + r, c + r), fill=(16, 16, 15))
    light = math.radians(-45)
    for k in range(36):   # knurls, brighter where they face the light
        a = 2 * math.pi * k / 36
        facing = math.cos(a - light)
        v = int(30 + 26 * max(0.0, facing))
        x0, y0 = c + r * 0.80 * math.sin(a), c - r * 0.80 * math.cos(a)
        x1, y1 = c + r * 0.99 * math.sin(a), c - r * 0.99 * math.cos(a)
        dr.line((x0, y0, x1, y1), fill=(v, v, v - 2), width=max(1, int(d / 46)))
    rc = r * 0.76
    dr.ellipse((c - rc - 1, c - rc - 1, c + rc + 1, c + rc + 1), fill=(8, 8, 8))
    cap = np.zeros((s, s, 4), np.float32)
    yy, xx = np.mgrid[0:s, 0:s].astype(np.float32)
    dist = np.hypot(xx - c, yy - c)
    inside = dist <= rc
    lx, ly = c - rc * 0.38, c - rc * 0.42
    spec = np.exp(-(((xx - lx) ** 2 + (yy - ly) ** 2) / (2 * (rc * 0.42) ** 2)))
    shade = 22 + 46 * spec - 10 * (dist / rc)
    cap[..., 0] = shade
    cap[..., 1] = shade
    cap[..., 2] = shade * 0.97
    cap[..., 3] = np.where(inside, 255, 0)
    capim = Image.fromarray(np.clip(cap, 0, 255).astype(np.uint8), "RGBA")
    im.alpha_composite(capim)
    dr = ImageDraw.Draw(im)
    dr.arc((c - rc, c - rc, c + rc, c + rc), 200, 290, fill=(95, 95, 92), width=max(1, int(d / 60)))
    return im


def knob_frame(body, deg, pointer_len=0.72, selector=False):
    """The knob turned to deg (0 = up): only the pointer moves; the light stays put."""
    im = body.copy()
    s = im.width
    c = s / 2
    r = s / 2 / 1.08
    a = math.radians(deg)
    dr = ImageDraw.Draw(im)
    if selector:
        # a chicken-head bar across the cap, pointed end lit
        p0 = (c - math.sin(a) * r * 0.55, c + math.cos(a) * r * 0.55)
        p1 = (c + math.sin(a) * r * 0.95, c - math.cos(a) * r * 0.95)
        dr.line((p0, p1), fill=(8, 8, 8), width=int(r * 0.42))
        dr.line((c, c, p1[0], p1[1]), fill=PLATE_TEXT, width=max(2, int(r * 0.10)))
        return im
    x0, y0 = c + math.sin(a) * r * 0.18, c - math.cos(a) * r * 0.18
    x1, y1 = c + math.sin(a) * r * 0.97, c - math.cos(a) * r * 0.97
    dr.line((x0 + 1, y0 + 1, x1 + 1, y1 + 1), fill=(0, 0, 0), width=max(2, int(r * 0.11)))
    dr.line((x0, y0, x1, y1), fill=(232, 226, 210), width=max(2, int(r * 0.08)))
    return im


def lamp_key(w, h, label, lit, size=13):
    """A dark key cap with an amber lamp strip, its label below the lamp (2x px)."""
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    pad = 3 * SS
    box = (pad, pad, w - pad, h - pad)
    shadow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle((box[0] + SS, box[1] + 2 * SS, box[2] + SS, box[3] + 2 * SS), radius=4 * SS, fill=(0, 0, 0, 180))
    im.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(2 * SS)))
    # the cap: a vertical gradient, lighter at the top
    cap = Image.new("RGBA", (box[2] - box[0], box[3] - box[1]))
    ch = cap.height
    top, bot = (64, 72, 66) if not lit else (78, 84, 72), (30, 36, 33) if not lit else (40, 42, 34)
    for yy in range(ch):
        t = yy / max(1, ch - 1)
        col = tuple(int(top[i] * (1 - t) + bot[i] * t) for i in range(3)) + (255,)
        ImageDraw.Draw(cap).line((0, yy, cap.width, yy), fill=col)
    m = Image.new("L", cap.size, 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, cap.width - 1, cap.height - 1), radius=4 * SS, fill=255)
    im.paste(cap, (box[0], box[1]), m)
    d.rounded_rectangle(box, radius=4 * SS, outline=(12, 14, 13), width=SS)
    d.line((box[0] + 4 * SS, box[1] + SS, box[2] - 4 * SS, box[1] + SS), fill=(120, 128, 118), width=SS)
    # the lamp strip
    lh = max(5 * SS, int(h * 0.12))
    lbox = (box[0] + 7 * SS, box[1] + 6 * SS, box[2] - 7 * SS, box[1] + 6 * SS + lh)
    if lit:
        glow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        ImageDraw.Draw(glow).rounded_rectangle(lbox, radius=2 * SS, fill=AMBER + (210,))
        im.alpha_composite(glow.filter(ImageFilter.GaussianBlur(4 * SS)))
        d.rounded_rectangle(lbox, radius=2 * SS, fill=AMBER)
        d.rectangle((lbox[0] + 2 * SS, lbox[1] + SS, lbox[2] - 2 * SS, lbox[1] + 2 * SS), fill=(255, 230, 170))
    else:
        d.rounded_rectangle(lbox, radius=2 * SS, fill=AMBER_DIM, outline=(30, 22, 10), width=SS)
    f = font(size)
    lines = label.split("\n")
    ty = (lbox[3] + box[3]) / 2 - (len(lines) - 1) * size * SS * 0.55
    for i, ln in enumerate(lines):
        y = ty + i * size * SS * 1.1
        d.text((w / 2 + SS, y + SS), ln, font=f, fill=(0, 0, 0, 170), anchor="mm")
        d.text((w / 2, y), ln, font=f, fill=(244, 238, 222) if lit else (172, 166, 148), anchor="mm")
    return im


# =================================================================================================
#  Output
# =================================================================================================
def crop_hi(tab, x0, y0, w, h):
    return tab.hi.crop((int(x0 * SS), int(y0 * SS), int((x0 + w) * SS), int((y0 + h) * SS)))


def final(im, w, h):
    return im.convert("RGB").resize((w, h), Image.LANCZOS)


def strip(frames, w, h):
    out = Image.new("RGB", (w, h * len(frames)))
    for i, f in enumerate(frames):
        out.paste(f, (0, i * h))
    return out


class Out:
    def __init__(self):
        os.makedirs(ART, exist_ok=True)
        for f in os.listdir(ART):
            if f.endswith(".png"):
                os.remove(os.path.join(ART, f))
        os.makedirs(PREVIEW, exist_ok=True)
        self.lines = []

    def save(self, im, name):
        im.save(os.path.join(ART, name), optimize=True)


def render(tab, out):
    bg = final(tab.hi, W, H)
    prev = bg.convert("RGBA")
    slug = tab.name.lower().replace(" ", "_")
    out.lines.append("")
    out.lines.append("[tab %s]" % tab.name)
    out.lines.append("art file=art/%s.png fit=stretch" % slug)
    for c in tab.controls:
        k, key = c["kind"], c["key"]
        if k == "knob":
            r = c["r"]
            side = 2 * r + 10
            body = knob_body(2 * r * SS)
            frames = []
            n = c["n"]
            for i in range(n):
                deg = c["degs"][i] if c.get("pointer") else -135 + 270 * i / (n - 1)
                kn = knob_frame(body, deg, selector=c.get("pointer", False))
                bgc = crop_hi(tab, c["cx"] - side / 2, c["cy"] - side / 2, side, side)
                bgc.alpha_composite(kn, ((bgc.width - kn.width) // 2, (bgc.height - kn.height) // 2))
                frames.append(final(bgc, side, side))
            out.save(strip(frames, side, side), key + ".png")
            prev.alpha_composite(frames[0 if not c.get("pointer") else 0].convert("RGBA"), (int(c["cx"] - side / 2), int(c["cy"] - side / 2)))
            vs = "vs=16 ink_dim=%s" % VFD_HEX if c["value"] else "vs=0"
            out.lines.append('knob cx=%d cy=%d r=%d label="" key=%s strip=art/%s.png frames=%d ns=0 %s bw=%d' %
                             (c["cx"], c["cy"] + Y_OFF, r, key, key, n, vs, c["bw"]))
        elif k in ("pair", "button"):
            w, h = c["w"], c["h"]
            imgs = []
            for st in (0, 1):
                bgc = crop_hi(tab, c["cx"] - w / 2, c["cy"] - h / 2, w, h)
                bgc.alpha_composite(lamp_key(w * SS, h * SS, c["label"], bool(st), size=14))
                imgs.append(final(bgc, w, h))
            out.save(imgs[0], key + "_off.png")
            out.save(imgs[1], key + "_on.png")
            prev.alpha_composite(imgs[0].convert("RGBA"), (int(c["cx"] - w / 2), int(c["cy"] - h / 2)))
            kind = "toggle" if k == "pair" else "button"
            out.lines.append('%s cx=%d cy=%d label="" key=%s img=art/%s_off.png img_on=art/%s_on.png w=%d h=%d ns=0' %
                             (kind, c["cx"], c["cy"] + Y_OFF, key, key, key, w, h))
        elif k == "keys":
            rects = c["rects"]
            x0 = min(r[0] for r in rects)
            y0 = min(r[1] for r in rects)
            x1 = max(r[0] + r[2] for r in rects)
            y1 = max(r[1] + r[3] for r in rects)
            files = []
            for i in range(len(c["labels"])):
                bgc = crop_hi(tab, x0, y0, x1 - x0, y1 - y0)
                for j, (rx, ry, rw, rh) in enumerate(rects):
                    kimg = lamp_key((rw + 2) * SS, rh * SS, c["labels"][j], j == i, size=c["label_size"])
                    bgc.alpha_composite(kimg, (int((rx - x0 - 1) * SS), int((ry - y0) * SS)))
                f = final(bgc, x1 - x0, y1 - y0)
                out.save(f, "%s_%d.png" % (key, i))
                files.append("art/%s_%d.png" % (key, i))
                if i == 0:
                    prev.alpha_composite(f.convert("RGBA"), (int(x0), int(y0)))
            out.lines.append('picture x=%d y=%d w=%d h=%d key=%s files="%s"' % (x0, y0 + Y_OFF, x1 - x0, y1 - y0, key, ",".join(files)))
            opts = ",".join([" "] * len(c["labels"]))
            rows = " rows=%d" % c["rows"] if c["rows"] > 1 else ""
            out.lines.append('enum_h cx=%d cy=%d label="" key=%s options="%s" sw=%d sh=%d%s img=art/clear.png img_on=art/clear.png' %
                             (c["cx"], c["cy"] + Y_OFF, key, opts, c["sw"], c["sh"], rows))
        elif k == "readout":
            out.lines.append('readout cx=%d cy=%d w=%d h=%d label="" key=%s tsize=%d tcolor=%s talign=center' %
                             (c["cx"], c["cy"] + Y_OFF, c["w"], c["h"], key, c["tsize"], VFD_HEX))
    for title, keys in tab.qlinks:
        out.lines.append('qlinks "%s" = %s' % (title, ",".join(keys)))
    bg.save(os.path.join(ART, slug + ".png"), optimize=True)
    prev.convert("RGB").save(os.path.join(PREVIEW, slug + ".png"))
    return bg


# =================================================================================================
#  The tabs
# =================================================================================================
def tab_kit():
    t = Tab("KIT", [("KIT", ["kit", "kit_load", "master_gain", "master_limiter", "scene_view", "xfade", "xfade_curve"])], 101)
    t.panel(14, 64, 760, 270, "KIT SELECT")
    t.knob("kit", 104, 196, 42, "KIT", bw=112, value=False)
    t.readout("kit", 450, 170, 500, 60, tsize=30, name="LOADED KIT  /  SELECTED KIT")
    t.button("kit_load", 450, 270, "LOAD KIT", w=200, h=56)
    t.panel(14, 344, 760, 270, "STATUS")
    t.readout("status", 394, 440, 700, 52, tsize=22, name="PLANT STATUS")
    t.tag(250, 545, ["YOUR KITS: SAVE A .TBKIT ON THE MAC, COPY IT TO", "Synths/.../Twisted Breaks/data/Presets/Kits", "THEN TURN THE KIT KNOB."], 430, 72)
    t.panel(786, 64, 480, 270, "MASTER")
    t.knob("master_gain", 900, 196, 42, "LEVEL", bw=120)
    t.toggle("master_limiter", 1120, 196, "LIMITER", w=150, h=62)
    t.panel(786, 344, 480, 270, "SCENES")
    t.keys("scene_view", 880, 440, ["A", "B"], 60, 56, name="PADS")
    t.knob("xfade", 1120, 450, 42, "CROSSFADER", bw=120)
    t.keys("xfade_curve", 880, 555, ["SMOOTH", "SHARP"], 92, 44, name="CURVE", label_size=12)
    return t


def tab_pad():
    keys = ["pad", "sp_gain", "sp_pitch", "sp_pan", "sp_start", "sp_end", "sp_velsens", "sp_slice",
            "sp_mode", "sp_stretch", "sp_char", "sp_reverse", "sp_choke", "sp_swap", "sp_swapq", "sp_group"]
    t = Tab("PAD", [("PAD", keys)], 202)
    t.panel(14, 64, 420, 228, "SELECTED PAD")
    t.knob("pad", 96, 186, 40, "PAD", value=False)
    t.readout("pad", 292, 176, 238, 56, tsize=24, name="PAD  /  SAMPLE")
    t.panel(446, 64, 820, 228, "SOUND")
    for i, (k, n) in enumerate([("sp_gain", "GAIN"), ("sp_pitch", "PITCH"), ("sp_pan", "PAN"),
                                ("sp_start", "START"), ("sp_end", "END"), ("sp_velsens", "VELOCITY")]):
        t.knob(k, 528 + i * 131, 176, 36, n, bw=116)
    t.panel(14, 302, 1252, 312, "PLAY")
    t.selector("sp_mode", 120, 410, 34, ["ONE", "LOOP", "SLICE", "SEQ 1", "SEQ L", "CONT"], "MODE")
    t.selector("sp_stretch", 330, 410, 34, ["OFF", "PITCH", "BEATS", "TEXT", "AKAI", "AK.R"], "STRETCH")
    t.selector("sp_char", 540, 410, 34, ["CLEAN", "SP12", "MPC60", "S950", "LO-FI"], "CHARACTER")
    t.knob("sp_slice", 730, 402, 34, "SLICE", bw=108)
    t.toggle("sp_reverse", 900, 402, "REVERSE", w=140, h=58)
    t.keys("sp_group", 1110, 402, ["A", "B", "C", "D"], 56, 52, name="GROUP")
    t.keys("sp_choke", 190, 556, ["OFF", "1", "2", "3", "4"], 62, 50, name="CHOKE")
    t.keys("sp_swap", 560, 556, ["OFF", "1", "2", "3", "4"], 62, 50, name="SWAP GROUP")
    t.keys("sp_swapq", 940, 556, ["SLICE", "1/16", "BEAT", "BAR"], 78, 50, name="SWAP AT", label_size=12)
    return t


def tab_sequencer():
    keys = ["seq_run", "seq_length", "seq_res", "seq_fill", "mut_go", "mut_amount", "snapshot", "bank_active"]
    t = Tab("SEQUENCER", [("SEQUENCER", keys)], 303)
    t.panel(14, 64, 760, 250, "SEQUENCER")
    t.toggle("seq_run", 110, 186, "RUN", w=130, h=62, name="SEQUENCER")
    t.knob("seq_length", 280, 178, 40, "LENGTH", bw=110)
    t.keys("seq_res", 480, 186, ["1/8", "1/16", "1/32"], 70, 56, name="STEP")
    t.toggle("seq_fill", 672, 186, "FILL", w=130, h=62, name="FILL")
    t.panel(786, 64, 480, 250, "MUTATE")
    t.button("mut_go", 900, 186, "MUTATE", w=170, h=70)
    t.knob("mut_amount", 1130, 178, 40, "AMOUNT", bw=110)
    t.panel(14, 324, 1252, 290, "SNAPSHOTS  &  BREAK BANK")
    t.keys("snapshot", 640, 420, ["OFF", "1", "2", "3", "4", "5", "6", "7", "8"], 112, 60, name="SNAPSHOT RECALL")
    t.keys("bank_active", 640, 548, ["1", "2", "3", "4", "5", "6", "7", "8"], 126, 60, name="BREAK BANK")
    return t


FX = [("fxmute", "MUTE"), ("lpf", "LPF"), ("hpf", "HPF"), ("bpf", "BPF"), ("crush", "CRUSH"), ("rate", "RATE"),
      ("drive", "DRIVE"), ("tape", "TAPE"), ("akaizer", "AKAIZER"), ("repeat", "REPEAT"), ("gate", "GATE"),
      ("pitch", "PITCH\nDROP"), ("delay", "DELAY"), ("reverb", "REVERB"), ("comb", "COMB"), ("reverse", "REVERSE")]


def tab_fx():
    t = Tab("FX", [("FX", ["fx_" + k for k, _ in FX])], 404)
    t.panel(14, 64, 1252, 110, "FX GROUP")
    t.keys("fx_group", 640, 126, ["GROUP A", "GROUP B", "GROUP C", "GROUP D"], 150, 52)
    t.panel(14, 184, 1252, 430, "PERFORMANCE FX")
    for i, (k, lab) in enumerate(FX):
        row, col = divmod(i, 8)
        t.toggle("fx_" + k, 104 + col * 153, 300 + row * 170, lab, w=138, h=96)
    return t


MACROS = [("destroy", "DESTROY"), ("mangle", "AMEN\nMANGLE"), ("rewind", "REWIND"), ("darken", "DARKEN"),
          ("tapestop", "TAPE\nSTOP"), ("dive", "PITCH\nDIVE"), ("roll", "JUNGLE\nROLL"), ("scifi", "SCI-FI")]


def tab_macros():
    keys = ["macro_" + k for k, _ in MACROS] + ["macro_amount", "macro_group", "macro_latch"]
    t = Tab("MACROS", [("MACROS", keys)], 505)
    t.panel(14, 64, 860, 550, "PERFORMANCE MACROS")
    for i, (k, lab) in enumerate(MACROS):
        row, col = divmod(i, 4)
        t.toggle("macro_" + k, 124 + col * 210, 220 + row * 220, lab, w=190, h=120)
    t.panel(886, 64, 380, 550, "MACRO CONTROL")
    t.knob("macro_amount", 1076, 196, 46, "AMOUNT", bw=130)
    t.keys("macro_group", 1076, 380, ["A", "B", "C", "D", "ALL"], 62, 54, name="GROUP")
    t.toggle("macro_latch", 1076, 520, "LATCH", w=150, h=66, name="KEYS")
    return t


def tab_mixer():
    keys = ["grp_a_level", "grp_b_level", "grp_c_level", "grp_d_level", "grp_a_mute", "grp_b_mute", "grp_c_mute", "grp_d_mute"]
    t = Tab("MIXER", [("MIXER", keys)], 606)
    for i, g in enumerate("abcd"):
        x = 14 + i * 316
        t.panel(x, 64, 304, 550, "GROUP " + g.upper())
        cx = x + 152
        t.knob("grp_%s_level" % g, cx, 250, 56, "LEVEL", bw=150)
        t.toggle("grp_%s_mute" % g, cx, 480, "MUTE", w=170, h=76)
    return t


def build():
    out = Out()
    Image.new("RGBA", (4, 4), (0, 0, 0, 0)).save(os.path.join(ART, "clear.png"))
    out.lines.append("# generated by skin/make_skin.py: Twisted Breaks' control-room skin for the MPC's 1280x628 plugin area.")
    out.lines.append("# Edit the script, not this file.")
    for k, v in (("bg", "0e1211"), ("panel", "1c2523"), ("line", "0e1211"), ("ink", "d9d2bd"), ("ink_dim", VFD_HEX),
                 ("accent", "ffa928"), ("accent_hi", "ffc865"), ("lcd", "0c0806"), ("seg_active", "0e1211"),
                 ("seg_inactive", "0e1211"), ("seg_active_tx", "0e1211"), ("box", "0c0806")):
        out.lines.append("theme_%s=%s" % (k, v))
    tiles = []
    for make in (tab_kit, tab_pad, tab_sequencer, tab_fx, tab_macros, tab_mixer):
        tab = make()
        bg = render(tab, out)
        tiles.append(bg)
        print("drew", tab.name)
    # the Instruments browser tile: the header's name plate and warning
    tiles[0].crop((0, 0, 820, 333)).resize((270, 110), Image.LANCZOS).save(os.path.join(ART, "tile.png"))
    open(os.path.join(VST, "layout.conf"), "w").write("\n".join(out.lines) + "\n")
    print("wrote vst/layout.conf and vst/art/ (%d files)" % len(os.listdir(ART)))


if __name__ == "__main__":
    build()
