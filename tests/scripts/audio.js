const aw = new ActiveXObject('AudioWizard');
aw.StartPeakmeterMonitoring(33, 50);
const outputs = JSON.parse(fb.GetOutputDevices());
const previous = outputs.find(o => o.active), silent = outputs.find(o => o.name === 'Null Output');
if (silent) fb.SetOutputDevice(silent.output_id, silent.device_id);
let samples = 0, level = -100, lufs = -100, corr = 0;
const active = plman.ActivePlaylist;
console.log('audio: ready playlist=' + (active >= 0 ? plman.GetPlaylistName(active) : 'none') + ' items=' + (active >= 0 ? plman.PlaylistItemCount(active) : 0));
window.SetTimeout(() => {
    fb.Play();
    console.log('audio: play');
}, 1000);
window.SetTimeout(() => {
    if (samples === 0) console.log('audio: no sound yet playing=' + fb.IsPlaying + ' time=' + fb.PlaybackTime.toFixed(1));
}, 6000);
const watch = window.SetInterval(() => {
    if (!fb.IsPlaying || fb.PlaybackTime < 1) return;
    level = Math.max(level, aw.PeakmeterAdjustedLeftRMS, aw.PeakmeterAdjustedRightRMS);
    lufs = Math.max(lufs, aw.MomentaryLUFS);
    corr = aw.PhaseCorrelation;
    if (++samples < 30) return;
    window.ClearInterval(watch);
    console.log('audio: level ' + level.toFixed(1) + ' lufs ' + lufs.toFixed(1) + ' corr ' + corr.toFixed(2));
    fb.Stop();
    if (silent && previous) fb.SetOutputDevice(previous.output_id, previous.device_id);
}, 100);
