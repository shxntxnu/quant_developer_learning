#ifndef TC_MODULE_H
#define TC_MODULE_H

#include "tc_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configuration passed to module during creation */
typedef struct TcModuleConfig {
    const char* module_name;
    const char* config_json; /* JSON string containing module settings */
} TcModuleConfig;

typedef enum TcModuleState {
    TC_MOD_STATE_UNINITIALIZED = 0,
    TC_MOD_STATE_READY         = 1,
    TC_MOD_STATE_RUNNING       = 2,
    TC_MOD_STATE_DEGRADED      = 3,
    TC_MOD_STATE_HALTED        = 4,
    TC_MOD_STATE_STOPPED       = 5
} TcModuleState;

/* Health & status monitoring report */
typedef struct TcModuleStatus {
    int32_t  state;          /* TcModuleState */
    uint32_t reserved;       /* Alignment padding */
    uint64_t events_processed;
    uint64_t error_count;
    char     status_msg[128];
} TcModuleStatus;

/* The uniform Virtual Table exported by EVERY plugin DLL */
typedef struct TcModuleVTable {
    uint32_t struct_size;    /* sizeof(TcModuleVTable) */
    uint32_t abi_version;    /* Must match TC_ABI_VERSION */
    const char* name;        /* e.g., "tc_risk", "tc_strategy" */
    
    /* Lifecycle operations */
    TcStatus (*create)(const TcModuleConfig* config, TcHandle* out);
    TcStatus (*start)(TcHandle handle);
    TcStatus (*stop)(TcHandle handle);
    void     (*destroy)(TcHandle handle);
    
    /* Diagnostics */
    TcStatus (*get_status)(TcHandle handle, TcModuleStatus* out);
    
    /* Interface discovery: queries specific interface tables (e.g. "ITcRisk") */
    void*    (*get_interface)(TcHandle handle, const char* interface_name);
} TcModuleVTable;

/* Mandatory export signature for all DLLs */
TC_API const TcModuleVTable* tc_get_module_vtable(void);

typedef const TcModuleVTable* (*TcGetModuleVTableFn)(void);

#ifdef __cplusplus
}
#endif

#endif /* TC_MODULE_H */
