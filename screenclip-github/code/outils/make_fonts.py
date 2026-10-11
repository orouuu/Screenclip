from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent
OUT = ROOT.parent / "include" / "fonts_data.h"

TEXT = "".join(chr(c) for c in range(32, 127)) + "éèêëàâùûçôîïÉÈÀÇ’…•°"
DIGITS = "0123456789"

ASCII = "".join(chr(c) for c in range(32, 127))

FONTS = [
    ("kFontLabel", "FiraSans-Medium.ttf", 24, TEXT, True),
    ("kFontSup", "FiraSans-Medium.ttf", 15, TEXT, True),
    ("kFontTitle", "FiraSans-Medium.ttf", 30, TEXT, True),
    ("kFontHeader", "FiraSans-Bold.ttf", 56, TEXT, True),
    ("kFontLine", "FiraSans-Bold.ttf", 36, TEXT, True),
    ("kFontVistaTitle", "selawkl.ttf", 58, TEXT, True),
    ("kFontVistaText", "selawksl.ttf", 26, TEXT, True),
    ("kFontVistaSmall", "selawksl.ttf", 20, TEXT, True),
    ("kFontVistaMedium", "selawksl.ttf", 40, TEXT, True),
    ("kFontBig", "FiraSans-Bold.ttf", 166, DIGITS + " ", True),
]

def render(name, file, size, chars, smooth):
    font = ImageFont.truetype(str(ROOT / "fonts" / file), size)
    ascent, descent = font.getmetrics()
    glyphs, data = [], bytearray()
    for ch in sorted(set(chars)):
        adv = round(font.getlength(ch))
        box = font.getbbox(ch)
        w, h = box[2] - box[0], box[3] - box[1]
        if w <= 0 or h <= 0:
            glyphs.append((ord(ch), 0, 0, 0, 0, adv, len(data)))
            continue
        img = Image.new("L", (w, h), 0)
        draw = ImageDraw.Draw(img)
        if not smooth:
            draw.fontmode = "1"
        draw.text((-box[0], -box[1]), ch, font=font, fill=255)
        glyphs.append((ord(ch), w, h, box[0], box[1] - ascent, adv, len(data)))
        data += img.tobytes()
    lines = [f"// {file} {size}px", f"static const uint8_t {name}Data[] = {{"]
    for i in range(0, len(data), 24):
        lines.append("  " + ",".join(str(b) for b in data[i:i + 24]) + ",")
    lines.append("};")
    lines.append(f"static const Glyph {name}Glyphs[] = {{")
    for g in glyphs:
        lines.append("  {%d, %d, %d, %d, %d, %d, %d}," % g)
    lines.append("};")
    lines.append(f"static const Font {name} = {{{name}Glyphs, {len(glyphs)}, {name}Data, {ascent}, {ascent + descent}}};")
    return "\n".join(lines)

def main():
    parts = [
        "// Fichier généré par outils/make_fonts.py — ne pas modifier à la main.",
        "// Polices : Fira Sans et Selawik (SIL Open Font License 1.1).",
        "#pragma once",
        '#include "texte.h"',
        "",
    ]
    for f in FONTS:
        parts.append(render(*f))
        parts.append("")
    OUT.write_text("\n".join(parts))
    print(f"{OUT} : {OUT.stat().st_size // 1024} Ko")

if __name__ == "__main__":
    main()
