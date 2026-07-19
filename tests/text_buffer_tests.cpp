#include "vijai/text_buffer.hpp"

#include <cassert>
#include <stdexcept>

namespace {

void text_buffer_tests() {
  auto buffer = vijai::TextBuffer::from_text("one\r\ntwo\r\nthree");
  assert(buffer.line_ending() == vijai::LineEnding::crlf);
  assert(buffer.line_count() == 3U);

  buffer.insert(3U, " and a half");
  assert(buffer.text() == "one and a half\r\ntwo\r\nthree");
  buffer.erase(3U, 11U);
  assert(buffer.text() == "one\r\ntwo\r\nthree");
  assert(buffer.can_undo());
  const auto undo_erase = buffer.undo();
  assert(undo_erase);
  assert(undo_erase->inserted_bytes == 11U);
  assert(buffer.text() == "one and a half\r\ntwo\r\nthree");
  const auto undo_insert = buffer.undo();
  assert(undo_insert);
  assert(undo_insert->removed_bytes == 11U);
  assert(buffer.text() == "one\r\ntwo\r\nthree");
  assert(buffer.can_redo());
  const auto redo_insert = buffer.redo();
  assert(redo_insert);
  assert(redo_insert->inserted_bytes == 11U);
  assert(buffer.text() == "one and a half\r\ntwo\r\nthree");

  bool threw = false;
  try {
    buffer.erase(99U, 1U);
  } catch (const std::out_of_range&) {
    threw = true;
  }
  assert(threw);

  const auto classic_mac = vijai::TextBuffer::from_text("one\rtwo\rthree");
  assert(classic_mac.line_ending() == vijai::LineEnding::cr);
  assert(classic_mac.line_count() == 3U);

  const auto searchable = vijai::TextBuffer::from_text("alpha beta alpha");
  assert(searchable.find("alpha", 0U) == 0U);
  assert(searchable.find("alpha", 1U) == 11U);
  assert(searchable.find("alpha", 12U) == 0U);
  assert(!searchable.find("missing", 0U));
  assert(!searchable.find("", 0U));

  auto replaceable = vijai::TextBuffer::from_text("red green red");
  replaceable.replace(4U, 5U, "blue");
  assert(replaceable.text() == "red blue red");
  replaceable.undo();
  assert(replaceable.text() == "red green red");
  assert(replaceable.redo());
  assert(replaceable.text() == "red blue red");
  assert(replaceable.replace_all("red", "orange") == 2U);
  assert(replaceable.text() == "orange blue orange");
  replaceable.undo();
  assert(replaceable.text() == "red blue red");
}

struct RunTextBufferTests {
  RunTextBufferTests() { text_buffer_tests(); }
};

RunTextBufferTests run_text_buffer_tests;

}  // namespace
