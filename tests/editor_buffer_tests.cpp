#include "vijai/editor_buffer.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>

namespace {

void editor_buffer_tests() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("vijai-editor-buffer-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  vijai::EditorBuffer buffer(vijai::Document::create(root / "sample.txt"), root, false);
  buffer.document().buffer().insert(0U, "alpha");
  std::string error;
  assert(buffer.document().save(error));
  buffer.set_cursor(99U);
  assert(buffer.cursor() == 5U);

  assert(buffer.checkpoint(error));
  buffer.document().restore_text("changed");
  assert(buffer.restore_recovery());
  assert(buffer.document().buffer().text() == "alpha");
  buffer.set_top_line(7U);
  assert(buffer.save_session(error));

  const auto reopened = vijai::Document::open(root / "sample.txt", error);
  assert(reopened);
  vijai::EditorBuffer restored(std::move(*reopened), root, true);
  assert(restored.cursor() == 5U);
  assert(restored.top_line() == 7U);
  std::filesystem::remove_all(root);
}

struct RunEditorBufferTests {
  RunEditorBufferTests() { editor_buffer_tests(); }
};

RunEditorBufferTests run_editor_buffer_tests;

}  // namespace
