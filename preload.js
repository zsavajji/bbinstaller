const { contextBridge, ipcRenderer, webUtils } = require('electron');

contextBridge.exposeInMainWorld('bb', {
  device: () => ipcRenderer.invoke('device'),
  pick: () => ipcRenderer.invoke('pick'),
  info: file => ipcRenderer.invoke('info', webUtils.getPathForFile(file)),
  install: (paths, password) => ipcRenderer.invoke('install', paths, password),
  on: (channel, fn) => ipcRenderer.on(channel, (e, value) => fn(value)),
});
