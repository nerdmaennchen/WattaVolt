/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdlib>
#include "pico.h"

#include "cranc/platform/system.h"

void *operator new(std::size_t n)
{
    cranc::LockGuard lock;
    return std::malloc(n);
}

void *operator new[](std::size_t n)
{
    cranc::LockGuard lock;
    return std::malloc(n);
}

void operator delete(void *p)
{ 
    cranc::LockGuard lock;
    std::free(p);
}

void operator delete[](void *p) noexcept 
{
    cranc::LockGuard lock;
    std::free(p); 
}

#if __cpp_sized_deallocation

void operator delete(void *p, __unused std::size_t n) noexcept 
{
    cranc::LockGuard lock;
    std::free(p); 
}

void operator delete[](void *p, __unused std::size_t n) noexcept 
{
    cranc::LockGuard lock;
    std::free(p); 
}

#endif
