#pragma once

#include "event_queue.h"
#include <string>

// Wire format between the event generator and the server: one event per
// line of plain text, so it's easy to read, log and type by hand.
//
//   ATT   <student_id> <meal_id>
//   PREP  <meal_id> <item_id> <quantity> <prepared_by>
//   WASTE <meal_id> <item_id> <quantity>
//
// A text protocol means a half-written or corrupted line is just a line we
// can reject -- the server never trusts bytes from another process.
std::string encodeEvent(const Event& event);

// Strict parser. Returns false (and leaves `out` untouched) for anything
// that isn't exactly a valid event: unknown keyword, missing/extra fields,
// non-numeric or non-positive ids, non-finite or non-positive quantity.
// `error` (optional) receives a short reason.
bool decodeEvent(const std::string& line, Event& out, std::string* error = nullptr);
