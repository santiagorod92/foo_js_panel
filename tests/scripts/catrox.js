const fcl = 'CATROX_FCL';
console.log('catrox: layout ' + (utils.IsFile(fcl) ? 'found' : 'missing'));
function on_paint(gr) { gr.FillSolidRect(0, 0, window.Width, window.Height, 0xff202020); }
setTimeout(() => {
    if (window.Height <= 100) return;
    console.log('catrox: requested');
    panelsui.RunAction('SKIN:IMPORT_FCL:' + fcl);
}, 1500);
