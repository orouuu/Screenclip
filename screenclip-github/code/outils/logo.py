from pathlib import Path
from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent
OUT = ROOT.parent / "include" / "logo_image.h"
PNG = ROOT.parent.parent / "logos" / "screenclip-logo.png"
WIDTH = 640
BIG_WIDTH = 2400

def vgrad(w, h, stops):
    img = Image.new("RGB", (1, h))
    for y in range(h):
        t = y / max(1, h - 1)
        for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
            if p0 <= t <= p1:
                k = (t - p0) / max(1e-6, p1 - p0)
                img.putpixel((0, y), tuple(round(a + (b - a) * k) for a, b in zip(c0, c1)))
                break
    return img.resize((w, h))

def ink_mask():
    src = Image.open(ROOT / "logo.png").convert("RGBA")
    flat = Image.new("RGBA", src.size, "white")
    flat.alpha_composite(src)
    r, g, b, _ = flat.split()
    ink = ImageChops.invert(ImageChops.lighter(ImageChops.lighter(r, g), b))
    return ink.crop(ink.point(lambda v: 255 if v > 40 else 0).getbbox())

def render(width):
    scale = 2
    ink = ink_mask()
    tw = width * scale
    ink = ink.resize((tw, round(ink.height * tw / ink.width)), Image.LANCZOS)
    th = ink.height

    glass = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    glass.paste(vgrad(tw, th, [(0, (120, 205, 255)), (0.5, (28, 118, 214)), (1, (8, 50, 130))]), (0, 0), ink)
    shine = Image.new("L", (tw, th), 0)
    ImageDraw.Draw(shine).rectangle((0, 0, tw, int(th * 0.47)), fill=80)
    glass.paste(Image.new("RGB", (tw, th), (255, 255, 255)), (0, 0), ImageChops.multiply(shine, ink))

    rh = int(th * 0.50)
    mirror = glass.transpose(Image.FLIP_TOP_BOTTOM).crop((0, 0, tw, rh))
    fade = vgrad(tw, rh, [(0, (60,) * 3), (0.9, (0, 0, 0)), (1, (0, 0, 0))]).convert("L")
    mirror.putalpha(ImageChops.multiply(mirror.getchannel("A"), fade))

    radius = max(4, tw // 110)
    pad = radius * 3
    gap = max(2, th // 20)
    canvas = Image.new("RGBA", (tw + 2 * pad, th + gap + rh + 2 * pad), (70, 170, 245, 0))
    halo = Image.new("L", canvas.size, 0)
    halo.paste(ink, (pad, pad))
    halo = halo.filter(ImageFilter.GaussianBlur(radius)).point(lambda v: v * 55 // 100)
    canvas.putalpha(halo)
    canvas.alpha_composite(glass, (pad, pad))
    canvas.alpha_composite(mirror, (pad, pad + th + gap))
    final = canvas.resize((canvas.width // scale, canvas.height // scale), Image.LANCZOS)
    return final, pad // scale, th // scale

def main():
    big, _, _ = render(BIG_WIDTH)
    PNG.parent.mkdir(exist_ok=True)
    big.save(PNG)

    img, pad, text_h = render(WIDTH)
    w, h = img.size
    r, g, b, a = img.split()
    colors = [((rr & 0xF8) << 8) | ((gg & 0xFC) << 3) | (bb >> 3)
              for rr, gg, bb in zip(r.tobytes(), g.tobytes(), b.tobytes())]
    alpha = a.tobytes()
    lines = [
        "// Fichier généré par outils/logo.py — ne pas modifier à la main.",
        "#pragma once",
        "#include <stdint.h>",
        "",
        f"constexpr int kLogoW = {w};        // image entière (halo et reflet compris)",
        f"constexpr int kLogoH = {h};",
        f"constexpr int kLogoPad = {pad};       // marge du halo autour du lettrage",
        f"constexpr int kLogoTextH = {text_h};    // hauteur du lettrage seul",
        "static const uint16_t kLogoColor[] = {",
    ]
    for i in range(0, len(colors), 24):
        lines.append("  " + ",".join(str(c) for c in colors[i:i + 24]) + ",")
    lines += ["};", "static const uint8_t kLogoAlpha[] = {"]
    for i in range(0, len(alpha), 32):
        lines.append("  " + ",".join(str(v) for v in alpha[i:i + 32]) + ",")
    lines.append("};")
    OUT.write_text("\n".join(lines) + "\n")
    print(f"{OUT.name} : {w}x{h} (lettrage {WIDTH}x{text_h}) ; {PNG} : {big.width}x{big.height}")

if __name__ == "__main__":
    main()
