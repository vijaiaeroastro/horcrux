#include "vijai/image_preview.hpp"

#include <cassert>

namespace {

void image_preview_tests() {
  assert(vijai::is_previewable_image("screenshot.png"));
  assert(vijai::is_previewable_image("SCREENSHOT.PNG"));
  assert(!vijai::is_previewable_image("photo.jpg"));

  std::string error;
  const auto missing = vijai::load_image_preview("does-not-exist.png", error);
  assert(!missing);
  assert(!error.empty());
}

struct RunImagePreviewTests {
  RunImagePreviewTests() { image_preview_tests(); }
};

RunImagePreviewTests run_image_preview_tests;

}  // namespace
