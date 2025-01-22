/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdlib.h>
#include "pico.h"

#include "misc/interrupt_active.h"
#include "cranc/platform/system.h"

extern "C" {

extern void *REAL_FUNC(malloc)(size_t size);
extern void *REAL_FUNC(calloc)(size_t count, size_t size);
extern void *REAL_FUNC(realloc)(void *mem, size_t size);
extern void REAL_FUNC(free)(void *mem);

extern char __StackLimit; /* Set by linker.  */

static inline void check_alloc(__unused void *mem, __unused uint size) {
    // if (isr_active()) {
    //     __breakpoint();
    // }
    if (!mem || (((char *)mem) + size) > &__StackLimit) {
        panic("Out of memory");
    }
}

void *WRAPPER_FUNC(malloc)(size_t size) {
    cranc::LockGuard lock;
    void *rc = REAL_FUNC(malloc)(size);
    check_alloc(rc, size);
    return rc;
}

void *WRAPPER_FUNC(calloc)(size_t count, size_t size) {
    cranc::LockGuard lock;
    void *rc = REAL_FUNC(calloc)(count, size);
    check_alloc(rc, count * size);
    return rc;
}

void *WRAPPER_FUNC(realloc)(void *mem, size_t size) {
    cranc::LockGuard lock;
    void *rc = REAL_FUNC(realloc)(mem, size);
    check_alloc(rc, size);
    return rc;
}

void WRAPPER_FUNC(free)(void *mem) {
    cranc::LockGuard lock;
    REAL_FUNC(free)(mem);
}

}