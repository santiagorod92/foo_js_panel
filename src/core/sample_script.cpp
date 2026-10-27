#include "sample_script.h"

namespace jsp {

const char* sample_script() {
    return R"JS(
function RGB(r, g, b) { return 0xff000000 | (r << 16) | (g << 8) | b; }
function RGBA(r, g, b, a) { return ((a << 24) | (r << 16) | (g << 8) | b) >>> 0; }

const DT_CENTER = 0x1, DT_VCENTER = 0x4, DT_SINGLELINE = 0x20, DT_END_ELLIPSIS = 0x8000;
const fontTitle = gdi.Font("Segoe UI", 20, 1);
const fontInfo = gdi.Font("Segoe UI", 14, 0);
const fontHint = gdi.Font("Segoe UI", 12, 2);
const tfTitle = fb.TitleFormat("%title%");
const tfInfo = fb.TitleFormat("[%artist%][ - %album%]");
let hover = false;
let ticker = 0;

function format_time(s) {
    s = Math.max(0, Math.floor(s));
    return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0");
}

function on_paint(gr) {
    const w = window.Width, h = window.Height;
    gr.FillGradRect(0, 0, w, h, 90, RGB(40, 44, 52), RGB(22, 24, 28));
    const flags = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
    if (fb.IsPlaying) {
        gr.GdiDrawText(tfTitle.Eval(), fontTitle, RGB(240, 240, 240), 10, h / 2 - 48, w - 20, 30, flags);
        gr.GdiDrawText(tfInfo.Eval(), fontInfo, RGB(170, 180, 190), 10, h / 2 - 16, w - 20, 22, flags);
        const len = fb.PlaybackLength, pos = fb.PlaybackTime;
        const bw = Math.max(0, w - 40);
        gr.FillRoundRect(20, h / 2 + 16, bw, 6, 3, 3, RGB(60, 64, 72));
        if (len > 0) gr.FillRoundRect(20, h / 2 + 16, Math.round(bw * pos / len), 6, 3, 3, RGB(90, 160, 255));
        gr.GdiDrawText(format_time(pos) + " / " + format_time(len), fontInfo, RGB(150, 155, 165),
                       10, h / 2 + 28, w - 20, 22, flags);
    } else {
        gr.GdiDrawText("JS Panel", fontTitle, RGB(240, 240, 240), 10, h / 2 - 40, w - 20, 30, flags);
        gr.GdiDrawText("Nothing playing. Click to play.", fontInfo, RGB(170, 180, 190), 10, h / 2 - 6, w - 20, 22, flags);
    }
    if (hover) gr.DrawRect(1, 1, w - 3, h - 3, 1, RGBA(90, 160, 255, 255));
    gr.GdiDrawText("Right-click: choose a script file", fontHint, RGB(110, 115, 125), 10, h - 26, w - 20, 20, flags);
}

function on_mouse_move() { if (!hover) { hover = true; window.Repaint(); } }
function on_mouse_leave() { hover = false; window.Repaint(); }
function on_mouse_lbtn_up() { fb.PlayOrPause(); }
function on_playback_new_track() { window.Repaint(); }
function on_playback_stop() { window.Repaint(); }
function on_playback_pause() { window.Repaint(); }
function on_playback_time() { window.Repaint(); }
)JS";
}

}
