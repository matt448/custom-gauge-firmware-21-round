#!/usr/bin/env python3

import argparse
from pathlib import Path

from PIL import Image


def rgb888_to_rgb565(red: int, green: int, blue: int) -> int:
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert a 480x480 PNG image to big-endian RGB565 data."
    )
    parser.add_argument("source", type=Path, help="Path to the input PNG image")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="Path for the RGB565 output (default: input path with .rgb565 suffix)",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    source = args.source
    destination = args.output or source.with_suffix(".rgb565")

    image = Image.open(source).convert("RGBA")

    if image.size != (480, 480):
        raise ValueError(f"Expected 480x480 image, got {image.size}")

    output = bytearray()

    for red, green, blue, alpha in image.get_flattened_data():
        # Flatten transparent pixels onto black.
        if alpha != 255:
            red = red * alpha // 255
            green = green * alpha // 255
            blue = blue * alpha // 255

        pixel = rgb888_to_rgb565(red, green, blue)

        # Most ESP32 LCD configurations expect RGB565 in big-endian byte order.
        output.append((pixel >> 8) & 0xFF)
        output.append(pixel & 0xFF)

    destination.write_bytes(output)

    print(f"Wrote {destination}")
    print(f"Size: {len(output):,} bytes")


if __name__ == "__main__":
    main()