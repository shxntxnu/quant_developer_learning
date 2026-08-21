#ifndef MATHLIB_H
#define MATHLIB_H
#if defined(_WIN32) || defined(__CYGWIN__)
    #ifdef MATHLIB_EXPORTS
        #define MATHLIB_API __declspec(dllexport)
    #else
        #define MATHLIB_API __declspec(dllimport)
    #endif
#else
    #define MATHLIB_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
// Financial math example: Geometric Annuity Present Value
MATHLIB_API double calculate_geometric_annuity(double payment, double growth_rate, double discount_rate, int periods);
// Basic math example
MATHLIB_API int add(int a, int b);
#ifdef __cplusplus
}
#endif
#endif // MATHLIB_H