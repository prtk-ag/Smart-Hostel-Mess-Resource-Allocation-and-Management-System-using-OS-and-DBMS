#include "channel.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <csignal>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

namespace {

bool isPipeSpec(const std::string& spec, std::string& name) {
    if (spec.rfind("pipe:", 0) != 0 || spec.size() <= 5) return false;
    name = spec.substr(5);
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;   // keeps names safe to splice into a path
    }
    return true;
}

void stripCR(std::string& s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
}

// ---- stdio: works identically on every OS --------------------------------

class StdinReader : public LineReader {
public:
    bool readLine(std::string& line) override {
        if (!std::getline(std::cin, line)) return false;
        stripCR(line);
        return true;
    }
};

class StdoutWriter : public LineWriter {
public:
    bool writeLine(const std::string& line) override {
        std::cout << line << '\n';
        std::cout.flush();
        return static_cast<bool>(std::cout);
    }
};

#ifdef _WIN32
// ---- Windows named pipe ---------------------------------------------------

std::string winPipePath(const std::string& name) { return "\\\\.\\pipe\\shrm_" + name; }

class WinPipeReader : public LineReader {
public:
    explicit WinPipeReader(HANDLE h) : handle_(h) {}
    ~WinPipeReader() override {
        DisconnectNamedPipe(handle_);
        CloseHandle(handle_);
    }
    bool readLine(std::string& line) override {
        for (;;) {
            const std::size_t nl = buffer_.find('\n');
            if (nl != std::string::npos) {
                line = buffer_.substr(0, nl);
                buffer_.erase(0, nl + 1);
                stripCR(line);
                return true;
            }
            char chunk[4096];
            DWORD got = 0;
            if (!ReadFile(handle_, chunk, sizeof(chunk), &got, nullptr) || got == 0) {
                // Writer closed (ERROR_BROKEN_PIPE). Hand back a final unterminated line, if any.
                if (buffer_.empty()) return false;
                line.swap(buffer_);
                buffer_.clear();
                stripCR(line);
                return true;
            }
            buffer_.append(chunk, got);
        }
    }
private:
    HANDLE handle_;
    std::string buffer_;
};

class WinPipeWriter : public LineWriter {
public:
    explicit WinPipeWriter(HANDLE h) : handle_(h) {}
    ~WinPipeWriter() override { CloseHandle(handle_); }
    bool writeLine(const std::string& line) override {
        const std::string out = line + "\n";
        DWORD written = 0;
        return WriteFile(handle_, out.data(), static_cast<DWORD>(out.size()), &written, nullptr) &&
               written == out.size();
    }
private:
    HANDLE handle_;
};

#else
// ---- POSIX FIFO -------------------------------------------------------------

std::string fifoPath(const std::string& name) {
    return (std::filesystem::temp_directory_path() / ("shrm_" + name + ".fifo")).string();
}

class FifoReader : public LineReader {
public:
    FifoReader(std::string path, std::ifstream in) : path_(std::move(path)), in_(std::move(in)) {}
    ~FifoReader() override {
        in_.close();
        unlink(path_.c_str());
    }
    bool readLine(std::string& line) override {
        if (!std::getline(in_, line)) return false;
        stripCR(line);
        return true;
    }
private:
    std::string path_;
    std::ifstream in_;
};

class FifoWriter : public LineWriter {
public:
    explicit FifoWriter(std::ofstream out) : out_(std::move(out)) {}
    bool writeLine(const std::string& line) override {
        out_ << line << '\n';
        out_.flush();
        return static_cast<bool>(out_);
    }
private:
    std::ofstream out_;
};
#endif

} // namespace

std::unique_ptr<LineReader> openReader(const std::string& spec, std::string& error) {
    if (spec == "stdio") return std::make_unique<StdinReader>();

    std::string name;
    if (!isPipeSpec(spec, name)) {
        error = "channel must be 'stdio' or 'pipe:NAME' (letters, digits, _ and - only), got '" + spec + "'";
        return nullptr;
    }
#ifdef _WIN32
    HANDLE h = CreateNamedPipeA(winPipePath(name).c_str(), PIPE_ACCESS_INBOUND,
                                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = "CreateNamedPipe failed, error " + std::to_string(GetLastError());
        return nullptr;
    }
    if (!ConnectNamedPipe(h, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
        error = "ConnectNamedPipe failed, error " + std::to_string(GetLastError());
        CloseHandle(h);
        return nullptr;
    }
    return std::make_unique<WinPipeReader>(h);
#else
    const std::string path = fifoPath(name);
    unlink(path.c_str());   // stale FIFO from a crashed run
    if (mkfifo(path.c_str(), 0600) != 0) {
        error = "mkfifo(" + path + ") failed";
        return nullptr;
    }
    std::ifstream in(path);   // blocks here until a writer opens the other end
    if (!in) {
        unlink(path.c_str());
        error = "could not open FIFO " + path;
        return nullptr;
    }
    return std::make_unique<FifoReader>(path, std::move(in));
#endif
}

std::unique_ptr<LineWriter> openWriter(const std::string& spec, std::string& error, int wait_seconds) {
    if (spec == "stdio") return std::make_unique<StdoutWriter>();

    std::string name;
    if (!isPipeSpec(spec, name)) {
        error = "channel must be 'stdio' or 'pipe:NAME' (letters, digits, _ and - only), got '" + spec + "'";
        return nullptr;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(wait_seconds);
#ifdef _WIN32
    for (;;) {
        HANDLE h = CreateFileA(winPipePath(name).c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) return std::make_unique<WinPipeWriter>(h);
        const DWORD err = GetLastError();
        if ((err != ERROR_FILE_NOT_FOUND && err != ERROR_PIPE_BUSY) || std::chrono::steady_clock::now() > deadline) {
            error = "could not open pipe (is shrm_ipc_server running?), error " + std::to_string(err);
            return nullptr;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
#else
    std::signal(SIGPIPE, SIG_IGN);   // reader dying should be a failed write, not a killed process
    const std::string path = fifoPath(name);
    while (!std::filesystem::exists(path)) {
        if (std::chrono::steady_clock::now() > deadline) {
            error = "pipe " + path + " never appeared (is shrm_ipc_server running?)";
            return nullptr;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::ofstream out(path);   // blocks until the reader has it open
    if (!out) {
        error = "could not open FIFO " + path;
        return nullptr;
    }
    return std::make_unique<FifoWriter>(std::move(out));
#endif
}
