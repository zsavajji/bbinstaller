const { app, BrowserWindow, dialog, ipcMain } = require('electron');
const path = require('path');
const bb = require('./bb');

let win;
let device = null;
let busy = false;

// Watch for the USB network link every 2 s; only talk to the phone when a new link shows up,
// since repeated discovery makes it close port 80. Paused while installing.
async function poll() {
  const ips = bb.candidateIps();
  if (!busy && !(device && ips.includes(device.info.ip))) {
    let found = null;
    for (const ip of ips) if ((found = await bb.identify(ip))) break;
    if (found || device) {
      device = found && new bb.Device(found);
      win?.webContents.send('device', found);
    }
  }
  setTimeout(poll, 2000);
}

ipcMain.handle('device', () => device?.info ?? null);
ipcMain.handle('info', (e, file) => bb.barInfo(file));
ipcMain.handle('pick', async () => {
  const r = await dialog.showOpenDialog(win, {
    properties: ['openFile', 'multiSelections'],
    filters: [{ name: 'BlackBerry apps', extensions: ['bar'] }],
  });
  return r.filePaths.map(bb.barInfo);
});
ipcMain.handle('install', async (e, files, password) => {
  if (!device) throw new Error('No phone connected');
  busy = true;
  try {
    await device.login(password);
    await device.install(files, p => win.webContents.send('progress', p));
  } finally {
    busy = false;
  }
});

app.whenReady().then(() => {
  win = new BrowserWindow({
    width: 520, height: 560, minWidth: 420, minHeight: 420,
    title: 'BB Installer',
    webPreferences: { preload: path.join(__dirname, 'preload.js') },
  });
  win.removeMenu();
  win.loadFile('index.html');
  poll();
});
app.on('window-all-closed', () => app.quit());
