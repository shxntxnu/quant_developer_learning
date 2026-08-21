#include "MathLib.h"
#include <cmath>
MATHLIB_API double calculate_geometric_annuity(double payment, double g,
                                               double r, int n) {
  // Special case when discount rate == growth rate
  if (r == g) {
    return (payment * n) / (1.0 + r);
  }
  // Standard geometric annuity formula: P * [1 - ((1+g)/(1+r))^n] / (r - g)
  return payment * (1.0 - std::pow((1.0 + g) / (1.0 + r), n)) / (r - g);
}
MATHLIB_API int add(int a, int b) { return a + b; }