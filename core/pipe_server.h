#pragma once
#include <Windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>

#define PIPE_NAME "\\\\.\\pipe\\NaikoExecutor"
#define PIPE_BUFFER 65536

// message types between UI and DLL
enum class MsgType : uint8_t {
    EXECUTE     = 0x01,  // UI -> DLL: run this script
    CONSOLE_OUT = 0x02,  // DLL -> UI: print output
    STATUS      = 0x03,  // DLL -> UI: status update
    PING        = 0x04,  // UI -> DLL: are you alive
    PONG        = 0x05,  // DLL -> UI: yes
    MODULE_LIST = 0x06,  // DLL -> UI: list of loaded modules
    VERSION     = 0x07,  // DLL -> UI: roblox version string
};

struct PipeMessage {
    MsgType type;
    uint32_t length;
    std::string data;
};

class PipeServer {
public:
    static PipeServer& get() {
        static PipeServer instance;
        return instance;
    }

    void start() {
        running_ = true;
        server_thread_ = std::thread([this]() { server_loop(); });
        server_thread_.detach();
        log_("[Pipe] server started on " + std::string(PIPE_NAME));
    }

    void stop() {
        running_ = false;
        if (pipe_ != INVALID_HANDLE_VALUE) {
            DisconnectNamedPipe(pipe_);
            CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
        }
    }

    // send a message to the UI
    void send(MsgType type, const std::string& data = "") {
        if (pipe_ == INVALID_HANDLE_VALUE || !connected_) return;

        uint8_t  t   = static_cast<uint8_t>(type);
        uint32_t len = static_cast<uint32_t>(data.size());

        DWORD written;
        WriteFile(pipe_, &t,   1,   &written, nullptr);
        WriteFile(pipe_, &len, 4,   &written, nullptr);
        if (!data.empty())
            WriteFile(pipe_, data.c_str(), len, &written, nullptr);
        FlushFileBuffers(pipe_);
    }

    void send_console(const std::string& msg) { send(MsgType::CONSOLE_OUT, msg); }
    void send_status(const std::string& msg)  { send(MsgType::STATUS, msg); }

    void on_execute(std::function<void(const std::string&)> fn) { on_execute_ = fn; }
    void set_logger(std::function<void(const std::string&)> fn) { log_ = fn; }

    bool is_connected() const { return connected_; }

private:
    PipeServer() {
        log_ = [](const std::string& s) { OutputDebugStringA((s + "\n").c_str()); };
    }

    void server_loop() {
        while (running_) {
            pipe_ = CreateNamedPipeA(
                PIPE_NAME,
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                1,
                PIPE_BUFFER,
                PIPE_BUFFER,
                0,
                nullptr
            );

            if (pipe_ == INVALID_HANDLE_VALUE) {
                log_("[Pipe] CreateNamedPipe failed: " + std::to_string(GetLastError()));
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            log_("[Pipe] waiting for UI connection...");
            if (!ConnectNamedPipe(pipe_, nullptr)) {
                CloseHandle(pipe_);
                pipe_ = INVALID_HANDLE_VALUE;
                continue;
            }

            connected_ = true;
            log_("[Pipe] UI connected");
            send_status("ready");

            // read loop
            while (running_ && connected_) {
                PipeMessage msg;
                if (!read_message(msg)) {
                    connected_ = false;
                    break;
                }
                handle_message(msg);
            }

            DisconnectNamedPipe(pipe_);
            CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
            log_("[Pipe] UI disconnected — waiting for reconnect");
        }
    }

    bool read_message(PipeMessage& msg) {
        DWORD read;
        uint8_t type;
        if (!ReadFile(pipe_, &type, 1, &read, nullptr) || read == 0) return false;

        uint32_t len;
        if (!ReadFile(pipe_, &len, 4, &read, nullptr) || read != 4) return false;

        msg.type   = static_cast<MsgType>(type);
        msg.length = len;
        msg.data.clear();

        if (len > 0) {
            msg.data.resize(len);
            DWORD total = 0;
            while (total < len) {
                if (!ReadFile(pipe_, &msg.data[total], len - total, &read, nullptr))
                    return false;
                total += read;
            }
        }
        return true;
    }

    void handle_message(const PipeMessage& msg) {
        switch (msg.type) {
        case MsgType::EXECUTE:
            if (on_execute_) on_execute_(msg.data);
            break;
        case MsgType::PING:
            send(MsgType::PONG);
            break;
        default:
            break;
        }
    }

    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    std::atomic<bool> running_{ false };
    std::atomic<bool> connected_{ false };
    std::thread server_thread_;
    std::function<void(const std::string&)> on_execute_;
    std::function<void(const std::string&)> log_;
};
