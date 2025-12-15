#include <stdio.h>

#include <assert.h>
#include <iostream>

#define DEBUG

#include "conditional_file_stream.cpp"
#include "egz_solver.cpp"

#define TO_FILE true

template <typename R>
void findEGZs(int m_max = M_MAX, int m_min = 1) {
  string output_file_name = "EGZ_" + R::name() + ".tsv";
  ConditionalFileStream output_file(output_file_name, TO_FILE);

  EGZSolver<R> s;

  for (int i = 1; i < T_MAX(m_max); i++)
    output_file << "\t" << i;
  output_file << "\n";
  for (int m = m_min; m < m_max; m++) {
    output_file << m;
    if (R::skip(m)) {
      for (int j = 0; j < T_MAX(m_max) - 1; j++)
        output_file << "\t";
      output_file << "\n";
      continue;
    }
    for (int i = 0; i < m; i++)
      output_file << "\t";
    for (int t = T_MIN(m); t < T_MAX(m_max); t++) {
      if (t < T_MAX(m)) {
        int e = s.EGZ(t, m);
        if (max(e - t, -1) == -1) {
          if (t < T_MAX(m_max) - 1)
            output_file << "\t";
          continue;
        }
        cout << "EGZ(" << t << ", " << R::name() << ", " << m << ") = " << e << endl;
        cout << "EGZ-t = " << max(e - t, -1) << endl;
        cout << endl;
        output_file << e - t;
      }
      if (t < T_MAX(m_max) - 1)
        output_file << "\t";
    }
    if (m != m_max - 1)
      output_file << "\n";
  }
}

int main() {
  // basic testing
  EGZSolver<Znp<2, 2>> s;
  assert(s.EGZ(16, 8) == 33);
  assert(s.EGZ(17, 8) == 33);
  findEGZs<F4>();
}
