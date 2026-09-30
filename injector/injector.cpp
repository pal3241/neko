#include <Windows.h>
#include <TlHelp32.h>
#include <string>
#include <iostream>

// finds a process by name, returns PID or 0
static DWORD find_process(const char* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32 pe{ sizeof(pe) };
    DWORD pid = 0;

    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, name) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

// classic LoadLibrary injection
static bool inject(DWORD pid, const std::string& dll_path) {
    // get full absolute path
    char abs[MAX_PATH];
    if (!GetFullPathNameA(dll_path.c_str(), MAX_PATH, abs, nullptr)) {
        std::cerr << "[injector] GetFullPathName failed: " << GetLastError() << "\n";
        return false;
    }

    HANDLE proc = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
        FALSE, pid);

    if (!proc) {
        std::cerr << "[injector] OpenProcess failed: " << GetLastError() << "\n";
        return false;
    }

    size_t path_len = strlen(abs) + 1;
    void* remote_mem = VirtualAllocEx(proc, nullptr, path_len,
                                      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_mem) {
        std::cerr << "[injector] VirtualAllocEx failed: " << GetLastError() << "\n";
        CloseHandle(proc);
        return false;
    }

    if (!WriteProcessMemory(proc, remote_mem, abs, path_len, nullptr)) {
        std::cerr << "[injector] WriteProcessMemory failed: " << GetLastError() << "\n";
        VirtualFreeEx(proc, remote_mem, 0, MEM_RELEASE);
        CloseHandle(proc);
        return false;
    }

    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    auto load_lib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(k32, "LoadLibraryA"));

    HANDLE thread = CreateRemoteThread(proc, nullptr, 0,
                                       load_lib, remote_mem, 0, nullptr);
    if (!thread) {
        std::cerr << "[injector] CreateRemoteThread failed: " << GetLastError() << "\n";
        VirtualFreeEx(proc, remote_mem, 0, MEM_RELEASE);
        CloseHandle(proc);
        return false;
    }

    WaitForSingleObject(thread, 8000);

    DWORD exit_code = 0;
    GetExitCodeThread(thread, &exit_code);
    bool ok = exit_code != 0;

    CloseHandle(thread);
    VirtualFreeEx(proc, remote_mem, 0, MEM_RELEASE);
    CloseHandle(proc);

    return ok;
}

int main(int argc, char* argv[]) {
    std::string dll_path = (argc >= 2) ? argv[1] : "bin\\NaikoCore.dll";
    const char* target   = "RobloxPlayerBeta.exe";

    std::cout << "[Naiko Injector] looking for " << target << "...\n";

    DWORD pid = 0;
    // wait up to 15s for roblox to open
    for (int i = 0; i < 15; i++) {
        pid = find_process(target);
        if (pid) break;
        std::cout << "[Naiko Injector] waiting... (" << (i+1) << "/15)\n";
        Sleep(1000);
    }

    if (!pid) {
        std::cerr << "[Naiko Injector] roblox not found. open roblox first.\n";
        return 1;
    }

    std::cout << "[Naiko Injector] found roblox PID " << pid << "\n";
    std::cout << "[Naiko Injector] injecting " << dll_path << "...\n";

    // give roblox 1s to finish loading if it just opened
    Sleep(1000);

    if (inject(pid, dll_path)) {
        std::cout << "[Naiko Injector] injection successful\n";
        return 0;
    } else {
        std::cerr << "[Naiko Injector] injection failed\n";
        return 1;
    }
}
