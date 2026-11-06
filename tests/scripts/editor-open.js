const answer = 42, name = 'editor', re = /a+b/g;
function on_paint(gr) { gr.FillSolidRect(0, 0, window.Width, window.Height, 0xff203040); }
window.SetTimeout(() => {
    window.EditScript();
    console.log(`editor: open ${name} ${answer} ${re.test('aab')}`);
}, 300);
