long long smallestPowerBiggerThan(long long base, long long value) {
  long long i = 1;
  while (i <= value) {
    i *= base;
  }
  return i;
}

// Testing for all values of m < M_MAX and T_MIN(m) <= t < T_MAX(m)
const int M_MAX = 70;
int T_MIN(int m = 1) { return m; }
int T_MAX(int m = M_MAX) { return 120; }