const outside = 'C:\\Users\\tester\\AppData\\Local\\Temp\\EoleImport\\themes\\eole\\columnsUI_eole.fcl';
const fcl = utils.IsFile(outside) ? outside : fb.ProfilePath + 'themes/eole/columnsUI_eole.fcl';
console.log('import: layout ' + (utils.IsFile(fcl) ? 'found' : 'missing') + ' ' + (fcl === outside ? 'outside' : 'profile'));
function on_paint(gr) { gr.FillSolidRect(0, 0, window.Width, window.Height, 0xff202020); }
if (window.Height > 100 || window.Height === 0) setTimeout(() => {
    console.log('import: requested');
    panelsui.RunAction('SKIN:IMPORT_FCL:' + fcl);
}, 1500);
