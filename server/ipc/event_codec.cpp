#include "event_codec.h"

#include <cmath>
#include <iomanip>
#include <sstream>

std::string encodeEvent(const Event& e) {
    std::ostringstream out;
    out << std::setprecision(12);
    switch (e.type) {
        case Event::Type::Attendance:
            out << "ATT " << e.student_id << ' ' << e.meal_id;
            break;
        case Event::Type::Prep: {
            // The name is the last field, so it must not contain whitespace.
            std::string by = e.prepared_by.empty() ? "unknown" : e.prepared_by;
            for (char& c : by) if (c == ' ' || c == '\t') c = '_';
            out << "PREP " << e.meal_id << ' ' << e.item_id << ' ' << e.quantity << ' ' << by;
            break;
        }
        case Event::Type::Waste:
            out << "WASTE " << e.meal_id << ' ' << e.item_id << ' ' << e.quantity;
            break;
    }
    return out.str();
}

namespace {

bool fail(std::string* error, const char* why) {
    if (error) *error = why;
    return false;
}

bool readId(std::istringstream& in, int& value) {
    long long v = 0;
    if (!(in >> v) || v <= 0 || v > 1000000000LL) return false;
    value = static_cast<int>(v);
    return true;
}

bool readQty(std::istringstream& in, double& value) {
    double v = 0;
    if (!(in >> v) || !std::isfinite(v) || v <= 0.0) return false;
    value = v;
    return true;
}

// True if only whitespace is left -- anything else is trailing junk.
bool atEnd(std::istringstream& in) {
    in >> std::ws;
    return in.peek() == std::char_traits<char>::eof();
}

} // namespace

bool decodeEvent(const std::string& line, Event& out, std::string* error) {
    std::istringstream in(line);
    std::string kind;
    if (!(in >> kind)) return fail(error, "empty line");

    Event e;
    if (kind == "ATT") {
        e.type = Event::Type::Attendance;
        if (!readId(in, e.student_id) || !readId(in, e.meal_id)) return fail(error, "bad ATT ids");
    } else if (kind == "PREP") {
        e.type = Event::Type::Prep;
        if (!readId(in, e.meal_id) || !readId(in, e.item_id)) return fail(error, "bad PREP ids");
        if (!readQty(in, e.quantity)) return fail(error, "bad PREP quantity");
        if (!(in >> e.prepared_by)) return fail(error, "PREP missing name");
    } else if (kind == "WASTE") {
        e.type = Event::Type::Waste;
        if (!readId(in, e.meal_id) || !readId(in, e.item_id)) return fail(error, "bad WASTE ids");
        if (!readQty(in, e.quantity)) return fail(error, "bad WASTE quantity");
    } else {
        return fail(error, "unknown event type");
    }
    if (!atEnd(in)) return fail(error, "extra fields");
    out = e;
    return true;
}
