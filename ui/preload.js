const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('naiko', {
    inject:       ()             => ipcRenderer.send('inject'),
    execute:      (script)       => ipcRenderer.send('execute', script),
    ping:         ()             => ipcRenderer.send('ping'),
    getScripts:   ()             => ipcRenderer.invoke('get-scripts'),
    saveScript:   (name, code)   => ipcRenderer.send('save-script', { name, content: code }),
    deleteScript: (name)         => ipcRenderer.send('delete-script', name),

    onConsole: (cb) => ipcRenderer.on('console', (_, data) => cb(data)),
    onStatus:  (cb) => ipcRenderer.on('status',  (_, s)    => cb(s)),

    windowClose:    () => ipcRenderer.send('window-close'),
    windowMinimize: () => ipcRenderer.send('window-minimize'),
    windowMaximize: () => ipcRenderer.send('window-maximize'),
});
