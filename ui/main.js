const { app, BrowserWindow, ipcMain, shell } = require('electron');
const path  = require('path');
const net   = require('net');
const fs    = require('fs');
const { exec } = require('child_process');

const PIPE_NAME = '\\\\.\\pipe\\NaikoExecutor';
const SCRIPTS_DIR = path.join(app.getPath('userData'), 'scripts');

let win    = null;
let pipe   = null;
let status = 'disconnected'; // disconnected | connecting | ready

// -------------------------------------------------------
// window
// -------------------------------------------------------
function createWindow() {
    win = new BrowserWindow({
        width:  900,
        height: 600,
        minWidth:  800,
        minHeight: 520,
        frame: false,
        transparent: false,
        backgroundColor: '#0d0d0f',
        webPreferences: {
            preload: path.join(__dirname, 'preload.js'),
            contextIsolation: true,
            nodeIntegration: false,
        },
        icon: path.join(__dirname, 'assets', 'icon.ico'),
    });

    win.loadFile('index.html');
    win.setMenuBarVisibility(false);
}

// -------------------------------------------------------
// named pipe connection to DLL
// -------------------------------------------------------
function connectPipe() {
    if (pipe) return;
    status = 'connecting';
    win?.webContents.send('status', status);

    pipe = net.createConnection(PIPE_NAME, () => {
        status = 'ready';
        win?.webContents.send('status', status);
        win?.webContents.send('console', { type: 'status', msg: 'connected to executor' });
    });

    let buf = Buffer.alloc(0);

    pipe.on('data', (chunk) => {
        buf = Buffer.concat([buf, chunk]);
        while (buf.length >= 5) {
            const type = buf[0];
            const len  = buf.readUInt32LE(1);
            if (buf.length < 5 + len) break;
            const data = buf.slice(5, 5 + len).toString('utf8');
            buf = buf.slice(5 + len);
            handleDllMessage(type, data);
        }
    });

    pipe.on('error', () => {
        pipe = null;
        status = 'disconnected';
        win?.webContents.send('status', status);
        // retry in 2s
        setTimeout(connectPipe, 2000);
    });

    pipe.on('close', () => {
        pipe = null;
        status = 'disconnected';
        win?.webContents.send('status', status);
        setTimeout(connectPipe, 2000);
    });
}

function handleDllMessage(type, data) {
    switch (type) {
    case 0x02: // CONSOLE_OUT
        try {
            win?.webContents.send('console', JSON.parse(data));
        } catch {
            win?.webContents.send('console', { type: 'print', msg: data });
        }
        break;
    case 0x03: // STATUS
        win?.webContents.send('console', { type: 'status', msg: data });
        break;
    case 0x05: // PONG
        break;
    }
}

function sendToDll(type, data = '') {
    if (!pipe || status !== 'ready') return false;
    const dataBuf = Buffer.from(data, 'utf8');
    const msg = Buffer.alloc(5 + dataBuf.length);
    msg[0] = type;
    msg.writeUInt32LE(dataBuf.length, 1);
    dataBuf.copy(msg, 5);
    pipe.write(msg);
    return true;
}

// -------------------------------------------------------
// injector — injects our DLL into roblox
// -------------------------------------------------------
function injectDll() {
    const dllPath = path.join(__dirname, '..', 'bin', 'NaikoCore.dll');
    if (!fs.existsSync(dllPath)) {
        win?.webContents.send('console', {
            type: 'error',
            msg: 'NaikoCore.dll not found — build the C++ project first'
        });
        return;
    }

    // simple injector via powershell + rundll32 trick
    // in production replace with a proper injector exe
    const injectorPath = path.join(__dirname, '..', 'bin', 'injector.exe');
    if (fs.existsSync(injectorPath)) {
        exec(`"${injectorPath}" "${dllPath}"`, (err, stdout) => {
            if (err) {
                win?.webContents.send('console', { type: 'error', msg: 'injection failed: ' + err.message });
            } else {
                win?.webContents.send('console', { type: 'status', msg: 'injected — connecting...' });
                setTimeout(connectPipe, 1000);
            }
        });
    } else {
        win?.webContents.send('console', {
            type: 'error',
            msg: 'injector.exe not found — build the C++ project first'
        });
    }
}

// -------------------------------------------------------
// script hub — reads from userData/scripts/
// -------------------------------------------------------
function ensureScriptsDir() {
    if (!fs.existsSync(SCRIPTS_DIR)) fs.mkdirSync(SCRIPTS_DIR, { recursive: true });
}

function getLocalScripts() {
    ensureScriptsDir();
    return fs.readdirSync(SCRIPTS_DIR)
        .filter(f => f.endsWith('.lua') || f.endsWith('.txt'))
        .map(f => ({
            name: f.replace(/\.(lua|txt)$/, ''),
            file: f,
            content: fs.readFileSync(path.join(SCRIPTS_DIR, f), 'utf8')
        }));
}

// -------------------------------------------------------
// IPC handlers
// -------------------------------------------------------
ipcMain.on('inject',  () => injectDll());
ipcMain.on('execute', (_, script) => {
    if (!sendToDll(0x01, script)) {
        win?.webContents.send('console', {
            type: 'error',
            msg: 'not connected — inject first'
        });
    }
});
ipcMain.on('ping', () => sendToDll(0x04));
ipcMain.handle('get-scripts', () => getLocalScripts());
ipcMain.on('save-script', (_, { name, content }) => {
    ensureScriptsDir();
    fs.writeFileSync(path.join(SCRIPTS_DIR, name + '.lua'), content, 'utf8');
});
ipcMain.on('delete-script', (_, name) => {
    const p = path.join(SCRIPTS_DIR, name + '.lua');
    if (fs.existsSync(p)) fs.unlinkSync(p);
});
ipcMain.on('window-close',    () => app.quit());
ipcMain.on('window-minimize', () => win?.minimize());
ipcMain.on('window-maximize', () => {
    if (win?.isMaximized()) win.restore(); else win?.maximize();
});

// -------------------------------------------------------
// app lifecycle
// -------------------------------------------------------
app.whenReady().then(() => {
    createWindow();
    // try connecting immediately (if DLL already injected from prev session)
    setTimeout(connectPipe, 500);
});

app.on('window-all-closed', () => {
    if (pipe) pipe.destroy();
    app.quit();
});
