#include <stdio.h>

#include <assert.h>
#include <iostream>

#include "conditional_file_stream.cpp"
#include "egz_solver.cpp"

// #define DEBUG
#define TO_FILE true

template <typename R>
void findEGZs(int m_max = M_MAX) {
  // TODO: should this be R::name() instead of R::order?
  string output_file_name = "EGZ_" + to_string(R::order) + ".txt";
  ConditionalFileStream output_file(output_file_name, TO_FILE);

  EGZSolver<R> s;
  for (int m = 1; m < m_max; m++) {
    output_file << m << "\t";
    if (R::skip(m)) {
      output_file << "\n";
      continue;
    }
    for (int i = 0; i < m; i++)
      output_file << "\t";
    for (int t = T_MIN(m); t < T_MAX(m); t++) {
      int e = s.EGZ(t, m);
      if (max(e - t, -1) == -1)
        continue;
      cout << "EGZ(" << t << ", " << R::name() << ", " << m << ") = " << e
           << endl;
      cout << "EGZ-t = " << max(e - t, -1) << endl;
      cout << endl;
      output_file << e - t << "\t";
    }
    output_file << "\n";
  }
}

int main() {
  // basic testing
  EGZSolver<Znp<2, 2>> s;
  assert(s.EGZ(16, 8) == 33);
  assert(s.EGZ(17, 8) == 33);
}
