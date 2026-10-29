// Phase 8 tests: the event codec (round trip + rejecting bad input) and the
// named-pipe channel (a writer thread and a reader thread talking over a real
// OS pipe -- a FIFO on Linux/macOS, a Win32 named pipe on Windows).

#include "channel.h"
#include "event_codec.h"

#include <cmath>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
int g_checks = 0, g_failures = 0;
#define CHECK(cond, message)                                             \
    do {                                                                 \
        ++g_checks;                                                      \
        if (!(cond)) { ++g_failures; std::cout << "  FAIL: " << (message) << "   [" #cond "]\n"; } \
    } while (0)

void testRoundTrip() {
    std::cout << "[1] Codec round trip\n";
    const Event cases[] = {Event::makeAttendance(3, 2), Event::makePrep(2, 5, 0.75, "chef anil"),
                           Event::makeWaste(1, 4, 1.25)};
    for (const Event& e : cases) {
        Event back;
        CHECK(decodeEvent(encodeEvent(e), back), "encoded line decodes: " + encodeEvent(e));
        CHECK(back.type == e.type && back.student_id == e.student_id && back.meal_id == e.meal_id &&
              back.item_id == e.item_id && std::fabs(back.quantity - e.quantity) < 1e-9, "fields survive");
    }
    Event p;
    decodeEvent(encodeEvent(cases[1]), p);
    CHECK(p.prepared_by == "chef_anil", "spaces in a name become underscores");
    CHECK(decodeEvent("  ATT   4   1  \r", p) , "extra whitespace and trailing CR are tolerated");
}

void testRejects() {
    std::cout << "[2] Codec rejects malformed input\n";
    const char* bad[] = {"", "   ", "HELLO 1 2", "att 1 2", "ATT", "ATT 1", "ATT 1 2 3", "ATT x 2", "ATT 0 2", "ATT -1 2",
                         "ATT 1.5 2", "ATT 99999999999 1", "PREP 2 1 0.5", "PREP 2 1 -3 chef", "PREP 2 1 0 chef",
                         "PREP 2 1 nan chef", "PREP 2 1 inf chef", "PREP 2 1 abc chef", "PREP 2 1 1 chef extra",
                         "WASTE 2 1", "WASTE 2 1 -1", "WASTE 2 1 nan", "WASTE 2 1 1 1"};
    for (const char* line : bad) {
        Event e = Event::makeAttendance(9, 9);
        std::string why;
        const bool ok = decodeEvent(line, e, &why);
        CHECK(!ok, std::string("rejected: '") + line + "'");
        CHECK(e.student_id == 9 && e.meal_id == 9, "output untouched on failure");
        CHECK(ok || !why.empty(), "a reason is given");
    }
}

void testChannel() {
    std::cout << "[3] Named-pipe channel between two threads\n";
    const std::string spec = "pipe:shrm_test_" + std::to_string(std::hash<std::string>{}(__FILE__) % 100000);
    std::vector<std::string> sent, got;
    for (int i = 0; i < 500; ++i) sent.push_back(encodeEvent(Event::makePrep(1 + i % 3, 1 + i % 6, 0.5, "t")));

    std::string werr, rerr;
    std::thread writer([&] {
        auto w = openWriter(spec, werr);
        if (!w) return;
        for (const auto& s : sent) if (!w->writeLine(s)) break;
    });   // writer is destroyed here -> pipe closes -> reader sees end-of-stream
    auto r = openReader(spec, rerr);
    CHECK(r != nullptr, "reader opened: " + rerr);
    if (r) {
        std::string line;
        while (r->readLine(line)) got.push_back(line);
    }
    writer.join();
    CHECK(werr.empty(), "writer opened: " + werr);
    CHECK(got == sent, "500 lines arrive complete and in order (got " + std::to_string(got.size()) + ")");

    std::string e;
    CHECK(openReader("pipe:../etc", e) == nullptr && !e.empty(), "path-like pipe name refused");
    CHECK(openReader("tcp:80", e) == nullptr, "unknown channel type refused");
}
} // namespace

int main() {
    std::cout << "SHRM Phase 8 -- IPC tests\n=========================\n";
    testRoundTrip();
    testRejects();
    testChannel();
    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n"
              << (g_failures == 0 ? "PASS" : "FAIL") << "\n";
    return g_failures == 0 ? 0 : 1;
}
