#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>


// Default directory for the generated tables, relative to the working directory.
const std::string DEFAULT_OUTPUT_DIR = "Experimental tables";

// Writes to a file only if enabled.
struct ConditionalFileStream {
  std::fstream output_file;
  bool enabled;

  ConditionalFileStream(std::string file_name, bool en, std::string directory = DEFAULT_OUTPUT_DIR) : enabled(en) {
    if (!enabled)
      return;
    std::filesystem::path path = std::filesystem::path(directory) / file_name;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    output_file.open(path, std::ios::out | std::ios::trunc);
    if (!output_file.is_open()) {
      std::cerr << "error: could not open '" << path.string() << "' for writing" << std::endl;
      enabled = false;
    }
  }

  template <typename T>
  ConditionalFileStream &operator<<(const T &v) {
    if (enabled)
      output_file << v;
    return *this;
  }

  // Pushes what has been written so far to disk. A full table takes hours, so
  // the caller flushes after each row: a run that is interrupted, or watched
  // from another shell, then shows every row it actually finished instead of
  // whatever happened to be past the stream's buffer. Rows are small and cost
  // minutes to compute, so the flush is free by comparison.
  void flush() {
    if (enabled)
      output_file.flush();
  }
};
