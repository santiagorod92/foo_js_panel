const font = gdi.Font('Segoe UI', 16, 1);
let tiles = null;

function build() {
    const panel = gdi.CreateImage(180, 180);
    let g = panel.GetGraphics();
    g.FillGradRect(0, 0, 180, 180, 90, 0xff2b5876, 0xff4e4376);
    g.DrawEllipse(20, 20, 140, 140, 4, 0xffffffff);
    g.FillPolygon(0xffffcc00, 0, [90, 50, 125, 120, 55, 120]);
    g.DrawRoundRect(4, 4, 172, 172, 16, 16, 2, 0x80ffffff);
    g.GdiDrawText('offscreen', font, 0xffffffff, 0, 140, 180, 30, 0x1);
    panel.ReleaseGraphics(g);

    const glass = gdi.CreateImage(180, 60);
    g = glass.GetGraphics();
    g.FillRoundRect(0, 0, 180, 60, 12, 12, 0x80ffffff);
    g.GdiDrawText('50% white', font, 0xff000000, 0, 0, 180, 60, 0x1 | 0x4 | 0x20);
    glass.ReleaseGraphics(g);

    const art = utils.GetAlbumArtV2(fb.GetNowPlaying() || fb.GetFocusItem(), 0);
    const src = art || panel;
    const cover = src.Resize(180, 180);
    const mask = gdi.CreateImage(180, 180);
    g = mask.GetGraphics();
    g.FillSolidRect(0, 0, 180, 180, 0xffffffff);
    g.FillEllipse(0, 0, 180, 180, 0xff000000);
    mask.ReleaseGraphics(g);
    const masked = cover.ApplyMask(mask);

    const shadow = gdi.CreateImage(220, 220);
    g = shadow.GetGraphics();
    g.FillEllipse(20, 20, 180, 180, 0xc0000000);
    shadow.ReleaseGraphics(g);
    shadow.StackBlur(12);

    const turned = src.Resize(120, 80);
    turned.RotateFlip(1);

    const scheme = src.GetColourScheme(5);
    tiles = { panel, glass, cover, masked, shadow, turned, scheme };
    console.log('images: built art=' + !!art + ' masked=' + masked + ' scheme=' + scheme.length +
                ' turned=' + turned.Width + 'x' + turned.Height);
}

function on_paint(gr) {
    if (!tiles) build();
    for (let y = 0; y < window.Height; y += 16)
        for (let x = 0; x < window.Width; x += 16)
            gr.FillSolidRect(x, y, 16, 16, ((x + y) / 16) % 2 ? 0xffcccccc : 0xff999999);
    gr.DrawImage(tiles.panel, 10, 20, 180, 180, 0, 0, 180, 180);
    gr.DrawImage(tiles.glass, 10, 120, 180, 60, 0, 0, 180, 60);
    gr.DrawImage(tiles.shadow, 190, 10, 220, 220, 0, 0, 220, 220);
    gr.DrawImage(tiles.cover, 210, 30, 180, 180, 0, 0, 180, 180);
    gr.DrawImage(tiles.turned, 420, 20, 80, 120, 0, 0, 80, 120);
    tiles.scheme.forEach((c, i) => gr.FillSolidRect(420 + i * 30, 160, 26, 26, c));
    console.log('images: painted');
}
