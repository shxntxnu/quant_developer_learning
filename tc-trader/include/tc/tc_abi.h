#ifndef TC_ABI_H
#define TC_ABI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Major version in upper 16 bits, Minor in lower 16 bits: 1.0 */
#define TC_ABI_VERSION 0x00010000

/* Platform-independent dynamic library export/import macros */
#if defined(_WIN32) || defined(__CYGWIN__)
    #ifdef TC_EXPORTS
        #define TC_API __declspec(dllexport)
    #else
        #define TC_API __declspec(dllimport)
    #endif
#else
    #define TC_API __attribute__((visibility("default")))
#endif

/* Maximum symbol string length including null terminator (e.g., "AAPL", "ES_202612") */
#define TC_SYMBOL_MAX 16

/* Opaque pointer handle to encapsulate internal module state */
typedef struct TcHandle_* TcHandle;

/* Fixed-point price representation: price * 10,000 (4 decimal places)
 * Example: $152.3500 is stored as 1,523,500.
 * Eliminates floating-point rounding errors and non-deterministic comparisons. */
typedef int64_t TcPrice;

#define TC_PRICE_SCALE 10000LL
#define TC_PRICE_TO_DOUBLE(p) ((double)(p) / (double)TC_PRICE_SCALE)
#define TC_DOUBLE_TO_PRICE(d) ((TcPrice)((d) * (double)TC_PRICE_SCALE + ((d) >= 0 ? 0.5 : -0.5)))

/* Standard Error Return Codes across all DLL interfaces */
typedef enum TcStatus {
    TC_OK                   =  0,
    TC_ERR_INVALID_ARG      = -1,
    TC_ERR_ABI_MISMATCH     = -2,
    TC_ERR_NOT_CONNECTED    = -3,
    TC_ERR_QUEUE_FULL       = -4,
    TC_ERR_RISK_REJECT      = -5,
    TC_ERR_NOT_FOUND        = -6,
    TC_ERR_BUFFER_TOO_SMALL = -7,
    TC_ERR_INTERNAL         = -99
} TcStatus;

#ifdef __cplusplus
}
#endif

#endif /* TC_ABI_H */
