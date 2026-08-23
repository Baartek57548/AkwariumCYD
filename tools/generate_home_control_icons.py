#!/usr/bin/env python3
"""Generate deterministic Home Control icons from the repository-native mark."""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "apps" / "home_control"
BACKGROUND = (7, 25, 31, 255)
PRIMARY = (52, 211, 153, 255)
SECONDARY = (56, 189, 248, 255)
SURFACE = (236, 253, 245, 255)


def draw_icon(size: int, *, safe_zone: float = 0.74) -> Image.Image:
    scale = 4
    canvas = size * scale
    image = Image.new("RGBA", (canvas, canvas), BACKGROUND)
    draw = ImageDraw.Draw(image)
    margin = canvas * (1.0 - safe_zone) / 2.0
    radius = canvas * 0.24
    draw.rounded_rectangle(
        (margin, margin, canvas - margin, canvas - margin),
        radius=radius,
        fill=(12, 50, 58, 255),
    )

    center_x = canvas / 2
    center_y = canvas / 2
    ring_radius = canvas * 0.205
    stroke = max(4, round(canvas * 0.055))
    draw.ellipse(
        (
            center_x - ring_radius,
            center_y - ring_radius,
            center_x + ring_radius,
            center_y + ring_radius,
        ),
        outline=PRIMARY,
        width=stroke,
    )
    for angle, color in ((-90, SECONDARY), (30, PRIMARY), (150, SURFACE)):
        radians = math.radians(angle)
        x = center_x + math.cos(radians) * ring_radius
        y = center_y + math.sin(radians) * ring_radius
        node_radius = canvas * 0.066
        draw.ellipse(
            (x - node_radius, y - node_radius, x + node_radius, y + node_radius),
            fill=color,
        )

    home_width = canvas * 0.22
    home_top = center_y - canvas * 0.06
    home_bottom = center_y + canvas * 0.115
    roof_y = center_y - canvas * 0.145
    points = (
        (center_x - home_width, home_top),
        (center_x, roof_y),
        (center_x + home_width, home_top),
        (center_x + home_width * 0.75, home_top),
        (center_x + home_width * 0.75, home_bottom),
        (center_x - home_width * 0.75, home_bottom),
        (center_x - home_width * 0.75, home_top),
    )
    draw.polygon(points, fill=SURFACE)
    door_width = canvas * 0.045
    draw.rounded_rectangle(
        (
            center_x - door_width,
            center_y + canvas * 0.025,
            center_x + door_width,
            home_bottom,
        ),
        radius=canvas * 0.012,
        fill=(12, 50, 58, 255),
    )
    return image.resize((size, size), Image.Resampling.LANCZOS).convert("RGB")


def save(path: Path, size: int, *, safe_zone: float = 0.74) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    draw_icon(size, safe_zone=safe_zone).save(path, "PNG", optimize=True)


def main() -> None:
    android_sizes = {
        "mipmap-mdpi/ic_launcher.png": 48,
        "mipmap-hdpi/ic_launcher.png": 72,
        "mipmap-xhdpi/ic_launcher.png": 96,
        "mipmap-xxhdpi/ic_launcher.png": 144,
        "mipmap-xxxhdpi/ic_launcher.png": 192,
    }
    android_root = APP / "android" / "app" / "src" / "main" / "res"
    for relative, size in android_sizes.items():
        save(android_root / relative, size)

    ios_sizes = {
        "Icon-App-20x20@1x.png": 20,
        "Icon-App-20x20@2x.png": 40,
        "Icon-App-20x20@3x.png": 60,
        "Icon-App-29x29@1x.png": 29,
        "Icon-App-29x29@2x.png": 58,
        "Icon-App-29x29@3x.png": 87,
        "Icon-App-40x40@1x.png": 40,
        "Icon-App-40x40@2x.png": 80,
        "Icon-App-40x40@3x.png": 120,
        "Icon-App-60x60@2x.png": 120,
        "Icon-App-60x60@3x.png": 180,
        "Icon-App-76x76@1x.png": 76,
        "Icon-App-76x76@2x.png": 152,
        "Icon-App-83.5x83.5@2x.png": 167,
        "Icon-App-1024x1024@1x.png": 1024,
    }
    ios_root = APP / "ios" / "Runner" / "Assets.xcassets" / "AppIcon.appiconset"
    for name, size in ios_sizes.items():
        save(ios_root / name, size)

    web_root = APP / "web" / "icons"
    save(web_root / "Icon-192.png", 192)
    save(web_root / "Icon-512.png", 512)
    save(web_root / "Icon-maskable-192.png", 192, safe_zone=0.58)
    save(web_root / "Icon-maskable-512.png", 512, safe_zone=0.58)
    save(APP / "web" / "favicon.png", 32)
    save(ROOT / "design" / "home-control" / "home-control-mark.png", 1024)


if __name__ == "__main__":
    main()
