#pragma once

#ifdef __cplusplus
extern "C" {
#endif

static inline void __dmb() {}
static inline void __sev() {}
static inline void __wfe() {}

#ifdef __cplusplus
}
#endif
