#include "vijai/document.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

void document_tests() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("vijai-document-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(root);
  const auto new_file = root / "new.txt";
  const auto created = vijai::Document::create(new_file);
  assert(created.has_path());
  assert(!created.is_dirty());
  assert(created.path() == new_file);
  const auto source = root / "source.txt";
  {
    std::ofstream out(source, std::ios::binary);
    out << std::string("\xEF\xBB\xBF") + "alpha\r\nbeta";
  }

  std::string error;
  assert(!vijai::Document::open(root, error));
  assert(error.find("not a regular file") != std::string::npos);
  auto document = vijai::Document::open(source, error);
  assert(document);
  assert(document->encoding() == vijai::TextEncoding::utf8_bom);
  assert(document->buffer().line_ending() == vijai::LineEnding::crlf);
  assert(!document->is_dirty());
  document->buffer().insert(5U, "!");
  assert(document->is_dirty());
  assert(document->save(error));
  assert(!document->is_dirty());

  std::ifstream saved(source, std::ios::binary);
  const std::string bytes(std::istreambuf_iterator<char>(saved), {});
  assert(bytes == std::string("\xEF\xBB\xBF") + "alpha!\r\nbeta");

  const auto utf16 = root / "utf16.txt";
  {
    std::ofstream out(utf16, std::ios::binary);
    constexpr char utf16_bytes[] = {
        static_cast<char>(0xFF), static_cast<char>(0xFE), 'g', 0, 'r', 0, 'e', 0,
        'e', 0, 'n', 0, ' ', 0, static_cast<char>(0x3D), static_cast<char>(0xD8),
        static_cast<char>(0x80), static_cast<char>(0xDE)};
    out.write(utf16_bytes, static_cast<std::streamsize>(sizeof(utf16_bytes)));
  }
  auto utf16_document = vijai::Document::open(utf16, error);
  assert(utf16_document);
  assert(utf16_document->encoding() == vijai::TextEncoding::utf16_le);
  assert(utf16_document->buffer().text() == "green 🚀");
  utf16_document->buffer().insert(5U, "!");
  assert(utf16_document->save(error));
  auto reopened = vijai::Document::open(utf16, error);
  assert(reopened);
  assert(reopened->buffer().text() == "green! 🚀");

  std::filesystem::remove_all(root);
}

struct RunDocumentTests {
  RunDocumentTests() { document_tests(); }
};

RunDocumentTests run_document_tests;

}  // namespace
