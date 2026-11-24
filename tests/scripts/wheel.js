let total = 0;

function on_paint(gr) {
    gr.FillSolidRect(0, 0, window.Width, window.Height, 0xff336699);
}

function on_mouse_wheel(step) {
    total += step;
    console.log('wheel: step ' + step + ' total ' + total);
}

console.log('wheel: ready');
