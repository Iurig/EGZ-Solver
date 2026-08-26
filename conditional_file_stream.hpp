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

  // Pushes what has been written to disk. The caller flushes after each row,
  // so an interrupted run -- or one watched from another shell -- shows every
  // row it finished rather than whatever was past the buffer. A row costs
  // minutes to compute and a few hundred bytes, so the flush is free.
  void flush() {
    if (enabled)
      output_file.flush();
  }
};
