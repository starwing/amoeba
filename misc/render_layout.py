#!/usr/bin/env python3
"""Render a gui_layout_benchmark dump as a PNG.

Usage:
    render_layout.py layout.txt out.png [--scale 0.65] [--ref screen.png]
"""

import argparse
import os
import sys

from PIL import Image, ImageDraw, ImageFont

APP_W, APP_H = 2748, 1263
FONT = os.environ.get("LAYOUT_FONT",
                      "/System/Library/Fonts/Hiragino Sans GB.ttc")

BG = (12, 12, 14)
PANEL = {"panel.left": (27, 27, 27), "panel.mid": (21, 21, 23),
         "panel.right": (21, 21, 23)}
CARD = {
    "left.newchat": (58, 59, 64), "left.usage": (26, 26, 26),
    "left.thread.sel": (45, 45, 45), "mid.fee": (58, 58, 60),
    "chat.task": (41, 40, 45), "mid.composer": (44, 44, 46),
    "right.search": (30, 30, 32), "right.tab.file": (45, 45, 47),
}
INK = (208, 208, 212)
DIM = (150, 150, 156)
BOX = (86, 92, 102)
TEXT_BOX = (224, 158, 74)


def font_at(size):
    return ImageFont.truetype(FONT, max(6, int(size)))


def load(path):
    boxes = []
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        kind, x, y, w, h, clip, name = line.split("\t", 6)
        boxes.append(dict(kind=kind, x=int(x), y=int(y), w=int(w),
                          h=int(h), clip=int(clip), name=name))
    return boxes


def by_name(boxes, name):
    for b in boxes:
        if b["name"] == name:
            return b
    raise KeyError(name)


def text_font(draw, text, box_w, box_h, scale):
    size = box_h * scale * 0.95
    f = font_at(size)
    tw = draw.textlength(text, font=f)
    if tw > max(1, box_w * scale - 2):
        size = size * max(0.5, (box_w * scale - 2) / max(1, tw))
        f = font_at(size)
    return f


def draw_box(draw, b, ox, oy, scale, text_color=INK):
    x, y = ox + b["x"] * scale, oy + b["y"] * scale
    w, h = b["w"] * scale, b["h"] * scale
    rect = [x, y, x + w, y + h]
    if b["name"] in CARD:
        draw.rectangle(rect, fill=CARD[b["name"]])
    if b["kind"] == "T":
        draw.rectangle(rect, outline=TEXT_BOX)
        if b["name"] == "M":
            draw.text((x + w / 2, y + h / 2), b["name"], fill=(220, 140, 70),
                      font=text_font(draw, b["name"], b["w"], b["h"], scale),
                      anchor="mm")
            return
        f = text_font(draw, b["name"], b["w"], b["h"], scale)
        draw.text((x, y + h / 2), b["name"], fill=text_color, font=f,
                  anchor="lm")
    elif b["kind"] == "I":
        color = (220, 160, 60) if b["name"] == "icon.folder" \
            else (96, 106, 118)
        draw.rectangle(rect, outline=color, width=1)
    else:
        if b["name"].endswith(".content"):
            return
        draw.rectangle(rect, outline=BOX)


def draw_special(draw, boxes, ox, oy, scale):
    for b in boxes:
        if b["name"] in ("usage.bar",):
            x, y = ox + b["x"] * scale, oy + b["y"] * scale
            w, h = b["w"] * scale, b["h"] * scale
            draw.rounded_rectangle([x, y, x + w, y + h], radius=max(1, h / 2),
                                   fill=(240, 160, 45))
        elif b["name"] in ("usage.bar2",):
            x, y = ox + b["x"] * scale, oy + b["y"] * scale
            w, h = b["w"] * scale, b["h"] * scale
            draw.rounded_rectangle([x, y, x + w, y + h], radius=max(1, h / 2),
                                   fill=(90, 150, 240))
        elif b["name"] == "badge":
            x, y = ox + b["x"] * scale, oy + b["y"] * scale
            w, h = b["w"] * scale, b["h"] * scale
            draw.rounded_rectangle([x, y, x + w, y + h], radius=max(2, h / 2),
                                   fill=(235, 70, 80))


def draw_group(base, boxes, clip, view, ox, oy, scale):
    layer = Image.new("RGBA", base.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for b in boxes:
        if b["clip"] != clip:
            continue
        draw_box(d, b, ox, oy, scale)
        draw_special(d, [b], ox, oy, scale)
    mask = Image.new("L", base.size, 0)
    md = ImageDraw.Draw(mask)
    vx, vy = ox + view["x"] * scale, oy + view["y"] * scale
    vw, vh = view["w"] * scale, view["h"] * scale
    md.rectangle([vx, vy, vx + vw, vy + vh], fill=255)
    alpha = Image.composite(layer.getchannel("A"),
                            Image.new("L", base.size, 0), mask)
    layer.putalpha(alpha)
    base.alpha_composite(layer)


def handle(draw, x, y0, y1, color, label):
    draw.line([(x, y0), (x, y1)], fill=color, width=2)
    draw.ellipse([x - 4, y0 - 4, x + 4, y0 + 4], fill=color)
    draw.text((x + 6, y0 + 2), label, fill=color, font=font_at(11))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("layout")
    ap.add_argument("out")
    ap.add_argument("--scale", type=float, default=0.65)
    ap.add_argument("--ref", default=None)
    args = ap.parse_args()

    boxes = load(args.layout)
    win0 = by_name(boxes, "window")
    app_w, app_h = win0["w"], win0["h"]
    s = args.scale
    margin_x, margin_top = 24, 54
    W = int(app_w * s) + margin_x * 2
    H = int(app_h * s) + margin_top + 26
    img = Image.new("RGBA", (W, H), BG + (255,))
    d = ImageDraw.Draw(img)
    ox, oy = margin_x, margin_top


    for b in boxes:
        if b["name"] in PANEL:
            x, y = ox + b["x"] * s, oy + b["y"] * s
            d.rectangle([x, y, x + b["w"] * s, y + b["h"] * s],
                        fill=PANEL[b["name"]])

    if args.ref:
        ref = Image.open(args.ref).convert("RGB")
        ref = ref.crop((24, 208, 24 + app_w, 208 + app_h))
        ref = ref.resize((int(app_w * s), int(app_h * s)), Image.LANCZOS)
        ref = ref.convert("RGBA")
        ref.putalpha(120)
        img.alpha_composite(ref, (ox, oy))

    for b in boxes:
        if b["clip"] == 0:
            draw_box(d, b, ox, oy, s)
            draw_special(d, [b], ox, oy, s)

    left_view = by_name(boxes, "left.list.view")
    chat_view = by_name(boxes, "chat.view")
    right_view = by_name(boxes, "right.list.view")
    draw_group(img, boxes, 1, left_view, ox, oy, s)
    draw_group(img, boxes, 2, chat_view, ox, oy, s)
    draw_group(img, boxes, 3, right_view, ox, oy, s)

    d = ImageDraw.Draw(img)
    left = by_name(boxes, "panel.left")
    right = by_name(boxes, "panel.right")
    win = by_name(boxes, "window")
    win_box = win

    red = (255, 95, 86)
    cyan = (90, 200, 235)
    green = (120, 220, 130)
    for label, x in (("E1", left["x"] + left["w"]),
                     ("E2", right["x"])):
        handle(d, ox + x * s, oy, oy + app_h * s, red, label)
    for label, x in (("E3", chat_view["x"]),
                     ("E4", chat_view["x"] + chat_view["w"])):
        handle(d, ox + x * s, oy + chat_view["y"] * s,
               oy + (chat_view["y"] + chat_view["h"]) * s, cyan, label)
    handle(d, ox + win_box["x"] + win_box["w"] * s, oy,
           oy + app_h * s, green, "E5")
    d.line([(ox, oy + app_h * s), (ox + app_w * s, oy + app_h * s)],
           fill=green, width=2)
    d.text((ox + app_w * s - 30, oy + app_h * s - 16), "E6", fill=green,
           font=font_at(11))

    for label, v in (("L", left_view), ("C", chat_view), ("F", right_view)):
        x = ox + (v["x"] + v["w"]) * s - 18
        y = oy + v["y"] * s + 4
        d.ellipse([x - 7, y - 7, x + 7, y + 7], fill=(50, 90, 120),
                  outline=cyan)
        d.text((x, y), label, fill=(230, 240, 250), font=font_at(10),
               anchor="mm")

    d.text((margin_x, 14),
           "orange: text box   gray: container   red: sidecar resizes  "
           "cyan: text insets   green: window resize   blue: scroll lists",
           fill=(200, 200, 204), font=font_at(13))
    img.convert("RGB").save(args.out)
    print("wrote", args.out, img.size)


if __name__ == "__main__":
    sys.exit(main())
