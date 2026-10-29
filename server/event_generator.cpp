// Phase 8: the "sensor" side. A separate PROCESS that invents a stream of
// mess events (attendance swipes, prep logs, waste logs) and sends them to
// the server over a pipe. Software stand-in for the IoT bins and load cells
// in the reference papers.
//
// Usage: shrm_event_generator [--count N] [--seed S] [--delay-ms M]
//                             [--bad K] [--out stdio|pipe:NAME]
//   --bad K   also sends K deliberately malformed lines, to show that the
//             server validates what it receives from another process.
// Defaults: 200 events, seed 42, no delay, out=stdio.
// Event ids match the seed data: students 1-8, meals 1-3, items 1-6.

#include "channel.h"
#include "event_codec.h"

#include <chrono>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    int count = 200, bad = 0, delay_ms = 0;
    unsigned seed = 42;
    std::string out_spec = "stdio";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) { std::cerr << flag << " needs a value\n"; std::exit(2); }
            return argv[++i];
        };
        if (a == "--count") count = std::atoi(next("--count"));
        else if (a == "--seed") seed = static_cast<unsigned>(std::strtoul(next("--seed"), nullptr, 10));
        else if (a == "--delay-ms") delay_ms = std::atoi(next("--delay-ms"));
        else if (a == "--bad") bad = std::atoi(next("--bad"));
        else if (a == "--out") out_spec = next("--out");
        else { std::cerr << "unknown option " << a << "\n"; return 2; }
    }

    std::string error;
    auto out = openWriter(out_spec, error);
    if (!out) { std::cerr << "generator: " << error << "\n"; return 1; }

    std::mt19937 rng(seed);   // fixed seed -> the same stream every run, so results are checkable
    auto pick = [&](int lo, int hi) { return lo + static_cast<int>(rng() % static_cast<unsigned>(hi - lo + 1)); };

    static const char* bad_lines[] = {"HELLO 1 2", "ATT x 2", "ATT 1", "PREP 2 1 -3 chef", "WASTE 2 1 nan",
                                      "ATT 1 2 3", "", "PREP 2 1 0.5"};
    int sent = 0, att = 0, prep = 0, waste = 0, bad_sent = 0;
    const int total = count + bad;
    for (int i = 0; i < total && sent >= 0; ++i) {
        // Spread the bad lines evenly through the stream rather than bunching them at the end.
        const bool send_bad = bad > 0 && bad_sent < bad && (i % (total / bad) == total / bad - 1);
        std::string line;
        if (send_bad) {
            line = bad_lines[bad_sent % (sizeof(bad_lines) / sizeof(bad_lines[0]))];
            ++bad_sent;
        } else {
            const int roll = pick(1, 100);
            Event e;
            if (roll <= 40) { e = Event::makeAttendance(pick(1, 8), pick(1, 3)); ++att; }
            else if (roll <= 75) {
                e = Event::makePrep(pick(1, 3), pick(1, 6), pick(1, 10) / 10.0, "gen-" + std::to_string(pick(1, 3)));
                ++prep;
            } else { e = Event::makeWaste(pick(1, 3), pick(1, 6), pick(1, 5) / 10.0); ++waste; }
            line = encodeEvent(e);
        }
        if (!out->writeLine(line)) { std::cerr << "generator: reader went away\n"; return 1; }
        ++sent;
        if (delay_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
    std::cerr << "generator: sent " << sent << " lines (ATT " << att << ", PREP " << prep << ", WASTE " << waste
              << ", malformed " << bad_sent << ")\n";
    return 0;
}
