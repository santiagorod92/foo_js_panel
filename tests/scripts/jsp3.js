// ==PREPROCESSOR==
// @name "JS Panel JSP3 check"
// @import "lodash"
// ==/PREPROCESSOR==
var font = JSON.stringify({ Name: 'Segoe UI', Size: 22, Weight: 700 });
var painted = 0;
console.log('jsp3: loaded lodash=' + (typeof _.chunk) + ' chunks=' + _.chunk([1, 2, 3, 4], 2).length + ' dui=' + (JSON.parse(window.GetFontDUI(0)).Name.length > 0) +
            ' dark=' + (typeof window.IsDark) + ' files=' + (typeof utils.ListFiles(fb.ProfilePath).toArray().length) + ' now=' + (utils.Now() > 0));
function on_paint(gr) {
    gr.Clear(0xff202830);
    gr.FillRoundedRectangle(10, 10, window.Width - 20, 40, 8, 8, 0xff3a6ea5);
    gr.FillEllipse(30, 30, 10, 10, 0xffffffff);
    gr.WriteText('JScript Panel 3 script $rgb(255,0,0)', font, 0xffffffff, 50, 10, window.Width - 60, 40, 0, 2);
    gr.DrawRectangle(5, 5, window.Width - 10, window.Height - 10, 1, 0xff808080);
    gr.FillRectangle(10, 60, 200, 30, JSON.stringify({ Start: [0, 0], End: [200, 0], Stops: [[0, 0xffff0000], [0.5, 0xff00ff00], [1, 0xff0000ff]] }));
    gr.PushLayer(220, 60, 40, 30);
    gr.FillEllipse(240, 75, 40, 40, 0xffffcc00);
    gr.PopLayer();
    gr.WriteText('plain $rgb(255,80,80)red$rgb() back', font, 0xffffffff, 10, 100, window.Width - 20, 30);
    if (++painted === 1) console.log('jsp3: paint w=' + utils.CalcTextWidth2('JScript Panel 3 script', font));
}
