# Naiko Executor

Modular Roblox executor. If one module breaks, the rest keep running.
Adapts to Roblox weekly updates via signature scanning — no hardcoded offsets.

---

## Structure

```
executor/
├── core/           # DLL entry, lua state, pipe server
├── modules/        # independent modules (http, loadstring, console, ...)
├── offsets/        # sig scanner, offset manager, signatures.json
├── updater/        # version watcher, auto-rescan on roblox update
├── injector/       # injector.exe source
├── include/        # shared headers
├── ui/             # electron frontend
└── bin/            # build output (NaikoCore.dll, injector.exe)
```

---

## Build — C++ (DLL + injector)

### Requirements
- Visual Studio 2022 (with "Desktop development with C++" workload)
- CMake 3.20+

### Steps

```bat
cd executor
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

output goes to `bin/`:
- `NaikoCore.dll` — the executor DLL
- `injector.exe`  — injects the DLL into roblox

---

## Build — UI (Electron)

### Requirements
- Node.js 18+

### Steps

```bat
cd executor/ui
npm install
npm start          # run in dev
npm run build      # package to dist/
```

---

## Usage

1. Open Roblox (join any game)
2. Run `ui/` with `npm start` or the packaged exe
3. Click **⚡ Inject** in the UI
4. Paste your script in the editor
5. Press **▶ Execute** or Ctrl+Enter

---

## Adding a new module

1. Create `modules/yourmodule.cpp`
2. Inherit `ModuleBase`, implement `init()`, `shutdown()`, `name()`, `version()`
3. In `core/dllmain.cpp`:
   ```cpp
   #include "../modules/yourmodule.cpp"
   // ...
   ModuleLoader::get().register_module(std::make_shared<YourModule>());
   ```
4. Rebuild the DLL — everything else is automatic

---

## Updating signatures after a Roblox update

If Roblox updates and some features break:

1. Open `offsets/signatures.json`
2. Update the broken pattern(s) — use Cheat Engine or x64dbg to find the new bytes
3. Delete `offsets/offsets.json` (cache) so it rescans on next launch
4. Rebuild or just re-inject (the DLL reads signatures.json at runtime)

The version watcher auto-detects Roblox updates and rescans — if the signatures
are still valid after an update, nothing breaks automatically.

---

## Module status

| Module     | Status | What it does |
|------------|--------|--------------|
| console    | ✅     | hooks print/warn/error, pipes to UI |
| http       | ✅     | HttpGet, http.request |
| loadstring | ✅     | re-enables loadstring |
| drawing    | 🔜     | Drawing library |
| filesystem | 🔜     | readfile, writefile |
| misc       | 🔜     | getrawmetatable, hookfunction, etc. |
