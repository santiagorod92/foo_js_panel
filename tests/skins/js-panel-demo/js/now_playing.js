include('lib/palette.js');

const fonts = {
    title: gdi.Font('Segoe UI', 14, 1),
    info: gdi.Font('Segoe UI', 12, 0),
    small: gdi.Font('Segoe UI', 11, 0),
};
const tf = {
    title: fb.TitleFormat('%title%'),
    info: fb.TitleFormat('[%artist%][  ·  %album%][ (%date%)]'),
    tech: fb.TitleFormat('%codec%[  ·  %bitrate% kbps][  ·  %samplerate% Hz]'),
};
const line = DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
let hover = false;

function format_time(s) {
    s = Math.max(0, Math.floor(s));
    const h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60, sec = String(s % 60).padStart(2, '0');
    return h ? h + ':' + String(m).padStart(2, '0') + ':' + sec : m + ':' + sec;
}

function on_paint(gr) {
    const w = window.Width, h = window.Height;
    gr.FillSolidRect(0, 0, w, h, hover ? palette.hover : palette.bar);
    const left = 12, textW = Math.max(120, Math.floor(w * 0.45));
    const right = left + textW + 24, barW = Math.max(60, w - right - 12);

    if (!fb.IsPlaying) {
        gr.GdiDrawText('Stopped', fonts.title, palette.secondary, left, 8, textW, 22, line);
        gr.GdiDrawText('Click to play  ·  right-click: JS Panel menu', fonts.info, palette.dim, left, 30, textW, 18, line);
        gr.FillRoundRect(right, 26, barW, 4, 2, 2, palette.track);
        return;
    }

    gr.GdiDrawText(tf.title.Eval(), fonts.title, palette.text, left, 8, textW, 22, line);
    gr.GdiDrawText(tf.info.Eval(), fonts.info, palette.secondary, left, 30, textW, 18, line);

    const pos = fb.PlaybackTime, len = fb.PlaybackLength;
    const state = fb.IsPaused ? 'Paused' : 'Playing';
    gr.GdiDrawText(state + '  ·  ' + tf.tech.Eval(), fonts.small, palette.dim, right, 6, barW, 16, line);
    gr.FillRoundRect(right, 26, barW, 4, 2, 2, palette.track);
    if (len > 0) gr.FillRoundRect(right, 26, Math.max(4, Math.round(barW * pos / len)), 4, 2, 2, palette.accent);
    gr.GdiDrawText(format_time(pos), fonts.small, palette.secondary, right, 34, barW, 16, DT_LEFT | line);
    gr.GdiDrawText(len > 0 ? format_time(len) : 'live', fonts.small, palette.secondary, right, 34, barW, 16, DT_RIGHT | line);
}

function on_mouse_move() { if (!hover) { hover = true; window.Repaint(); } }
function on_mouse_leave() { hover = false; window.Repaint(); }
function on_mouse_lbtn_up() { fb.PlayOrPause(); }
function on_mouse_wheel(step) { if (step > 0) fb.Prev(); else fb.Next(); }

function on_playback_new_track() { window.Repaint(); }
function on_playback_stop() { window.Repaint(); }
function on_playback_pause() { window.Repaint(); }
function on_playback_time() { window.Repaint(); }

console.log('now_playing.js loaded (' + window.Width + 'x' + window.Height + ')');
