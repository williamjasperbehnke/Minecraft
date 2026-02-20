#!/usr/bin/env python3
from PIL import Image
import math

TILE = 16
COLS = 8
ROWS = 8
W = COLS * TILE
H = ROWS * TILE


def clamp(v):
    return max(0, min(255, int(v)))


def mix(a, b, t):
    return tuple(clamp(a[i] + (b[i] - a[i]) * t) for i in range(3)) + (255,)


def solid(img, idx, color):
    tx, ty = idx % COLS, idx // COLS
    ox, oy = tx * TILE, ty * TILE
    p = img.load()
    for y in range(TILE):
        for x in range(TILE):
            p[ox + x, oy + y] = color


def set_px(p, ox, oy, x, y, c):
    p[ox + x, oy + y] = c


def gradient(img, idx, top, bot):
    tx, ty = idx % COLS, idx // COLS
    ox, oy = tx * TILE, ty * TILE
    p = img.load()
    for y in range(TILE):
        t = y / (TILE - 1)
        c = mix(top, bot, t)
        for x in range(TILE):
            p[ox + x, oy + y] = c


def noise_tile(img, idx, base, a, b):
    tx, ty = idx % COLS, idx // COLS
    ox, oy = tx * TILE, ty * TILE
    p = img.load()
    for y in range(TILE):
        for x in range(TILE):
            h = (x * 37 + y * 71 + idx * 97) & 15
            c = base
            if h > 12:
                c = a
            elif h > 9:
                c = b
            p[ox + x, oy + y] = c


def stone_tile(img, idx, seed):
    tx, ty = idx % COLS, idx // COLS
    ox, oy = tx * TILE, ty * TILE
    p = img.load()
    pal = [(110, 110, 114, 255), (102, 102, 106, 255), (122, 122, 127, 255), (96, 96, 100, 255)]
    for y in range(TILE):
        for x in range(TILE):
            h = _h2(x + seed * 17, y + seed * 23, seed)
            i = int(h * 3.99)
            c = pal[i]
            # Light anti-repetition cracks
            if ((x * 5 + y * 3 + seed) % 19) == 0:
                c = (88, 88, 92, 255)
            set_px(p, ox, oy, x, y, c)


def stone_px(x, y):
    seed = 41
    pal = [(110, 110, 114, 255), (102, 102, 106, 255), (122, 122, 127, 255), (96, 96, 100, 255)]
    h = _h2(x + seed * 17, y + seed * 23, seed)
    c = pal[int(h * 3.99)]
    if ((x * 5 + y * 3 + seed) % 19) == 0:
        c = (88, 88, 92, 255)
    return c


def dirt_tile(img, idx, seed):
    tx, ty = idx % COLS, idx // COLS
    ox, oy = tx * TILE, ty * TILE
    p = img.load()
    pal = [(123, 87, 58, 255), (111, 78, 52, 255), (139, 98, 65, 255), (98, 69, 46, 255)]
    for y in range(TILE):
        for x in range(TILE):
            h = _h2(x + seed * 13, y + seed * 19, seed)
            c = pal[int(h * 3.99)]
            if ((x + y + seed) % 11) == 0:
                c = (c[0] + 8, c[1] + 6, c[2] + 4, 255)
            set_px(p, ox, oy, x, y, c)


def grass_top_tile(img, idx):
    tx, ty = idx % COLS, idx // COLS
    ox, oy = tx * TILE, ty * TILE
    p = img.load()
    for y in range(TILE):
        for x in range(TILE):
            n = _h2(x * 3 + 17, y * 5 + 31, 2)
            c = (68, 140, 58, 255)
            if n > 0.75:
                c = (92, 168, 74, 255)
            elif n < 0.2:
                c = (56, 118, 50, 255)
            if ((x + y) % 9) == 0:
                c = (c[0], clamp(c[1] + 10), c[2], 255)
            set_px(p, ox, oy, x, y, c)


def grass_side_tile(img, idx):
    tx, ty = idx % COLS, idx // COLS
    ox, oy = tx * TILE, ty * TILE
    p = img.load()
    for y in range(TILE):
        for x in range(TILE):
            if y < 4:
                n = _h2(x * 7 + 11, y * 5 + 19, 3)
                c = (72, 146, 62, 255) if n > 0.35 else (60, 128, 54, 255)
            else:
                n = _h2(x * 5 + 13, y * 7 + 29, 4)
                c = (122, 86, 58, 255) if n > 0.4 else (108, 76, 51, 255)
            set_px(p, ox, oy, x, y, c)


def sand_tile(img, idx):
    tx, ty = idx % COLS, idx // COLS
    ox, oy = tx * TILE, ty * TILE
    p = img.load()
    pal = [(224, 204, 148, 255), (214, 192, 136, 255), (236, 216, 164, 255), (200, 180, 124, 255)]
    for y in range(TILE):
        for x in range(TILE):
            h = _h2(x * 9 + 37, y * 11 + 73, 5)
            c = pal[int(h * 3.99)]
            if ((x - y) % 13) == 0:
                c = (clamp(c[0] - 10), clamp(c[1] - 8), clamp(c[2] - 6), 255)
            set_px(p, ox, oy, x, y, c)


def _h2(x, y, s):
    v = (x * 73856093) ^ (y * 19349663) ^ (s * 83492791)
    v &= 0xFFFFFFFF
    v ^= (v >> 13)
    v = (v * 1274126177) & 0xFFFFFFFF
    return (v & 0xFFFF) / 65535.0


def ore_tile(img, idx, ore_a, ore_b, ore_c, seed):
    tx, ty = idx % COLS, idx // COLS
    ox, oy = tx * TILE, ty * TILE
    p = img.load()
    ore = [ore_a, ore_b, ore_c]
    # Randomized micro-vein directions per tile.
    a0 = _h2(seed, 11, 91) * math.pi
    a1 = _h2(seed, 17, 133) * math.pi
    vx0, vy0 = math.cos(a0), math.sin(a0)
    vx1, vy1 = math.cos(a1), math.sin(a1)
    for y in range(TILE):
        for x in range(TILE):
            h = (x * 29 + y * 53 + seed * 41) & 255
            # Match exact base stone texture so ore blocks blend naturally.
            c = stone_px(x, y)
            # Base granular deposits.
            if h > 245:
                c = ore[0]
            elif h > 237:
                c = ore[1]
            elif h > 230:
                c = ore[2]
            # Vein bands (distance to directional lines) to break repetitive dots.
            fx = x - TILE * 0.5
            fy = y - TILE * 0.5
            d0 = abs(fx * (-vy0) + fy * vx0)
            d1 = abs(fx * (-vy1) + fy * vx1)
            n0 = _h2(x + seed * 3, y + seed * 7, 17)
            n1 = _h2(x + seed * 5, y + seed * 11, 23)
            if d0 < 0.95 and n0 > 0.52:
                c = ore[1]
            if d1 < 0.70 and n1 > 0.70:
                c = ore[0]
            if d0 < 0.45 and d1 < 0.45 and (n0 + n1) > 1.35:
                c = ore[0]
            p[ox + x, oy + y] = c


def bark_tile(img, idx, light, dark, knot):
    tx, ty = idx % COLS, idx // COLS
    ox, oy = tx * TILE, ty * TILE
    p = img.load()
    for y in range(TILE):
        for x in range(TILE):
            # Vertical grain with slight wave so trunks look less flat/repeating.
            wave = int(2.0 * math.sin((y + idx * 3) * 0.35))
            stripe = ((x + wave) // 2) & 1
            c = dark if stripe else light
            # Micro-shadowing for depth.
            if (x % 4) == 0:
                c = (clamp(c[0] - 8), clamp(c[1] - 8), clamp(c[2] - 8), 255)
            if (x % 4) == 2:
                c = (clamp(c[0] + 6), clamp(c[1] + 6), clamp(c[2] + 6), 255)
            # Knot flecks / scars.
            if ((x * 5 + y * 7 + idx) % 47) == 0:
                c = knot
            p[ox + x, oy + y] = c


def birch_bark_tile(img, idx):
    tx, ty = idx % COLS, idx // COLS
    ox, oy = tx * TILE, ty * TILE
    p = img.load()
    base = (228, 223, 203, 255)
    shade = (208, 202, 182, 255)
    scar = (74, 74, 72, 255)
    for y in range(TILE):
        for x in range(TILE):
            c = base if ((x + y) & 1) == 0 else shade
            # Vertical soft striations.
            if (x % 5) == 0:
                c = (clamp(c[0] - 6), clamp(c[1] - 6), clamp(c[2] - 6), 255)
            # Birch dark scar marks.
            if ((y * 3 + x * 7 + idx) % 19) == 0:
                c = scar
            if ((y * 5 + x * 11 + idx) % 43) == 0:
                c = (56, 56, 54, 255)
            p[ox + x, oy + y] = c


def log_top_tile(img, idx, outer, inner, ring):
    tx, ty = idx % COLS, idx // COLS
    ox, oy = tx * TILE, ty * TILE
    p = img.load()
    cx = (TILE - 1) * 0.5
    cy = (TILE - 1) * 0.5
    for y in range(TILE):
        for x in range(TILE):
            dx = x - cx
            dy = y - cy
            d = math.sqrt(dx * dx + dy * dy)
            c = outer if d > 5.8 else inner
            if abs((d % 2.0) - 1.0) < 0.18:
                c = ring
            set_px(p, ox, oy, x, y, c)


def leaves_tile(img, idx, c0, c1, c2, seed):
    tx, ty = idx % COLS, idx // COLS
    ox, oy = tx * TILE, ty * TILE
    p = img.load()
    for y in range(TILE):
        for x in range(TILE):
            h = (x * 17 + y * 29 + seed * 19) & 255
            c = c0
            if h > 210:
                c = c1
            elif h > 180:
                c = c2
            alpha = 255 if h > 35 else 0
            p[ox + x, oy + y] = (c[0], c[1], c[2], alpha)


def water_tile(img, idx):
    tx, ty = idx % COLS, idx // COLS
    ox, oy = tx * TILE, ty * TILE
    p = img.load()
    for y in range(TILE):
        for x in range(TILE):
            wave = ((x * 3 + y * 2 + int(6 * _h2(x, y, 77))) % 9) / 8.0
            r = clamp(34 + 20 * wave)
            g = clamp(86 + 34 * wave)
            b = clamp(164 + 56 * wave)
            a = 165
            p[ox + x, oy + y] = (r, g, b, a)


def ice_tile(img, idx):
    tx, ty = idx % COLS, idx // COLS
    ox, oy = tx * TILE, ty * TILE
    p = img.load()
    for y in range(TILE):
        for x in range(TILE):
            wave = ((x * 5 + y * 3 + int(5 * _h2(x + 13, y + 19, 91))) % 11) / 10.0
            r = clamp(138 + 28 * wave)
            g = clamp(186 + 34 * wave)
            b = clamp(224 + 20 * wave)
            a = 255
            p[ox + x, oy + y] = (r, g, b, a)


def cactus_side(img, idx):
    tx, ty = idx % COLS, idx // COLS
    ox, oy = tx * TILE, ty * TILE
    p = img.load()
    for y in range(TILE):
        for x in range(TILE):
            base = [(52, 132, 64, 255), (61, 151, 74, 255), (42, 112, 52, 255)][(x + y) % 3]
            if x in (0, TILE - 1):
                base = (32, 90, 42, 255)
            if (x % 4 == 0) and (y % 3 == 0):
                base = (78, 176, 90, 255)
            p[ox + x, oy + y] = base


def cactus_top(img, idx):
    noise_tile(img, idx, (83, 169, 95, 255), (104, 190, 115, 255), (63, 142, 74, 255))

def crack_tile(img, idx, stage):
    tx, ty = idx % COLS, idx // COLS
    ox, oy = tx * TILE, ty * TILE
    p = img.load()
    for y in range(TILE):
        for x in range(TILE):
            p[ox + x, oy + y] = (0, 0, 0, 0)

    # Dense pixel cracks that increase with stage (0..9).
    lines = 3 + stage
    for i in range(lines):
        a = _h2(i + stage * 3, 17, 101) * math.pi * 2.0
        oxi = _h2(i + stage * 5, 31, 111) * (TILE - 1)
        oyi = _h2(i + stage * 7, 47, 127) * (TILE - 1)
        vx = math.cos(a)
        vy = math.sin(a)
        length = 4 + int(_h2(i + stage, 53, 131) * 10)
        for t in range(-length, length + 1):
            x = int(round(oxi + vx * t))
            y = int(round(oyi + vy * t))
            if x < 0 or x >= TILE or y < 0 or y >= TILE:
                continue
            alpha = 38 + stage * 14
            p[ox + x, oy + y] = (236, 236, 236, alpha)
            if stage >= 4:
                x2 = x + (1 if ((i + t) & 1) == 0 else -1)
                if 0 <= x2 < TILE:
                    p[ox + x2, oy + y] = (210, 210, 210, alpha - 10)


def main():
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))

    # 0..9 base blocks
    solid(img, 0, (0, 0, 0, 0))  # air
    grass_side_tile(img, 1)  # grass side
    grass_top_tile(img, 2)  # grass top
    dirt_tile(img, 3, 31)  # dirt
    stone_tile(img, 4, 41)  # stone
    sand_tile(img, 5)  # sand
    water_tile(img, 6)  # water
    bark_tile(img, 7, (140, 108, 72, 255), (104, 78, 50, 255), (162, 126, 86, 255))  # oak side
    log_top_tile(img, 8, (176, 138, 94, 255), (154, 118, 80, 255), (188, 152, 108, 255))  # oak top
    leaves_tile(img, 9, (64, 132, 62), (86, 156, 80), (44, 108, 44), 9)  # leaves

    # 10..17 ores + gravel/clay
    ore_tile(
        img,
        10,
        (58, 58, 58, 255),
        (36, 36, 36, 255),
        (82, 82, 82, 255),
        10,
    )  # coal
    ore_tile(
        img,
        11,
        (194, 116, 63, 255),
        (228, 148, 83, 255),
        (139, 81, 49, 255),
        11,
    )  # copper
    ore_tile(
        img,
        12,
        (170, 138, 104, 255),
        (202, 168, 128, 255),
        (126, 98, 71, 255),
        12,
    )  # iron
    ore_tile(
        img,
        13,
        (212, 171, 61, 255),
        (247, 210, 100, 255),
        (150, 116, 39, 255),
        13,
    )  # gold
    ore_tile(
        img,
        14,
        (94, 217, 244, 255),
        (146, 242, 255, 255),
        (61, 160, 190, 255),
        14,
    )  # diamond
    ore_tile(
        img,
        15,
        (66, 195, 82, 255),
        (103, 233, 121, 255),
        (43, 140, 56, 255),
        15,
    )  # emerald
    noise_tile(img, 16, (123, 123, 126, 255), (112, 112, 114, 255), (137, 137, 140, 255))  # gravel
    noise_tile(img, 17, (138, 158, 168, 255), (130, 148, 157, 255), (151, 171, 181, 255))  # clay

    # 18..29 biome/wood blocks
    noise_tile(img, 18, (238, 242, 248, 255), (248, 251, 255, 255), (220, 228, 238, 255))  # snow
    ice_tile(img, 19)  # ice
    bark_tile(img, 20, (96, 74, 50, 255), (68, 52, 34, 255), (118, 92, 62, 255))  # spruce side
    log_top_tile(img, 21, (126, 102, 70, 255), (102, 80, 56, 255), (146, 122, 86, 255))  # spruce top
    leaves_tile(img, 22, (42, 88, 50), (62, 118, 70), (30, 64, 36), 22)  # spruce leaves
    birch_bark_tile(img, 23)  # birch side
    log_top_tile(img, 24, (204, 184, 146, 255), (182, 162, 126, 255), (222, 204, 164, 255))  # birch top
    leaves_tile(img, 25, (78, 134, 64), (108, 168, 86), (60, 106, 48), 25)  # birch leaves
    cactus_side(img, 26)  # cactus side
    cactus_top(img, 27)  # cactus top
    noise_tile(img, 28, (220, 198, 144, 255), (206, 184, 130, 255), (190, 166, 114, 255))  # sandstone
    for i in range(10):
        crack_tile(img, 32 + i, i)  # crack overlay stages 0..9

    img.save("assets/atlas.png")
    print("Generated assets/atlas.png", img.size)


if __name__ == "__main__":
    main()
