#pragma once

#include <memory>
#include <string>

// A one-way, line-oriented channel between two PROCESSES (the event
// generator writes, the server reads). Same interface everywhere; the
// operating system underneath differs:
//
//   "stdio"      standard output of one program piped into standard input of
//                the other:  shrm_event_generator | shrm_ipc_server
//                Works the same on Linux, macOS and Windows (cmd, PowerShell).
//   "pipe:NAME"  a NAMED pipe, which unrelated programs can open by name.
//                  Linux/macOS: a FIFO special file made with mkfifo()
//                               in the temp directory.
//                  Windows:     \\.\pipe\shrm_NAME (CreateNamedPipe).
//
// Windows has no mkfifo(), which is why this layer exists at all.
class LineReader {
public:
    virtual ~LineReader() = default;
    // Blocks until a full line arrives. Returns false at end-of-stream,
    // i.e. once the writer has closed its end. Trailing '\r' is stripped.
    virtual bool readLine(std::string& line) = 0;
};

class LineWriter {
public:
    virtual ~LineWriter() = default;
    // Writes line + newline and flushes so the reader sees it immediately.
    // Returns false if the reader has gone away.
    virtual bool writeLine(const std::string& line) = 0;
};

// The READER creates a named pipe (and removes it when destroyed). Opening
// blocks until a writer connects. On failure returns nullptr and sets `error`.
std::unique_ptr<LineReader> openReader(const std::string& spec, std::string& error);

// The WRITER connects to an existing named pipe, retrying for up to
// `wait_seconds` so the two programs can be started in either order.
std::unique_ptr<LineWriter> openWriter(const std::string& spec, std::string& error, int wait_seconds = 15);
