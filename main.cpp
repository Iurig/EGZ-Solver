#include <stdio.h>

#include <assert.h>
#include <fstream>
#include <iostream>

#include "egz_solver.cpp"

// #define DEBUG
#define TO_FILE false

template <typename R>
void findEGZs(int m_max = M_MAX) {
  EGZSolver<R> s;
  ofstream output_file;
  if (TO_FILE)
    output_file.open("EGZ_" + to_string(R::order) + ".txt");
  for (int m = 1; m < m_max; m++) {
    if (TO_FILE)
      output_file << m << "\t";
    if (R::skip(m)) {
      if (TO_FILE)
        output_file << "\n";
      continue;
    }
    if (TO_FILE)
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
      if (TO_FILE)
        output_file << e - t << "\t";
    }
    if (TO_FILE)
      output_file << "\n";
  }
}

int main() {
  // basic testing
  EGZSolver<Znp<2, 2>> s;
  assert(s.EGZ(16, 8) == 33);
  assert(s.EGZ(17, 8) == 33);
}
