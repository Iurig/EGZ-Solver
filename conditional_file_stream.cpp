#include <fstream>
#include <string>

using namespace std;

// Writes to a file only if enabled.
struct ConditionalFileStream {
  std::fstream output_file;
  bool enabled;

  ConditionalFileStream(std::string file_name, bool en) : enabled(en) {
    if (!enabled)
      return;
    output_file.open("F:\\Documents\\EGZ-Solver\\EGZ-Solver\\Experimental tables\\" + file_name, std::ios::out | std::ios::trunc);
  }

  template <typename T>
  ConditionalFileStream &operator<<(const T &v) {
    if (enabled)
      output_file << v;
    return *this;
  }
};
