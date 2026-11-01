function RGB(r, g, b) { return (0xff000000 | (r << 16) | (g << 8) | b) >>> 0; }

const palette = {
    bar: RGB(36, 38, 46),
    hover: RGB(40, 43, 52),
    text: RGB(225, 228, 235),
    secondary: RGB(160, 165, 175),
    dim: RGB(120, 125, 135),
    track: RGB(52, 55, 64),
    accent: RGB(0, 120, 215),
};

const DT_LEFT = 0x0, DT_CENTER = 0x1, DT_RIGHT = 0x2, DT_VCENTER = 0x4, DT_SINGLELINE = 0x20,
      DT_END_ELLIPSIS = 0x8000;
