const generation = 1;
console.log('reload: generation ' + generation);
function on_paint(gr) { gr.FillSolidRect(0, 0, window.Width, window.Height, generation === 1 ? 0xff203040 : 0xff402030); }
