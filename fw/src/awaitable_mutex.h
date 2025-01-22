#pragma once

#include "cranc/util/LinkedList.h"
#include <coroutine>

struct AwaitableMutex {
    struct Lock : cranc::util::LinkedList<Lock> {
        Lock(AwaitableMutex& mtx) : mutex{mtx} 
        {}
        AwaitableMutex& mutex;
        std::coroutine_handle<> handle{};

        bool await_ready() const { 
            cranc::LockGuard lock;
            return not mutex.locked and mutex.head.empty(); 
        }

        bool await_suspend(std::coroutine_handle<> h)
        {
            cranc::LockGuard lock;
            if (await_ready()) {
                mutex.locked = true;
                return false;
            }
            handle = h;
            mutex.head.insertBefore(this);
            return true;
        }

        auto await_resume() {
            struct Unlocker {
                Unlocker(AwaitableMutex& mtx) : mutex{mtx} 
                {}
                ~Unlocker() {
                    mutex.next();
                }
                AwaitableMutex& mutex;
            };
            cranc::LockGuard lock;
            mutex.locked = true;
            return Unlocker(mutex);
        }
    };

    cranc::util::LinkedList<Lock> head;
    bool locked{};

    auto operator co_await() {
        return Lock{*this};
    }

    void next() {
        Lock* n;
        {
            cranc::LockGuard lock;
            n = *(head.next);
            if (n == head) {
                locked = false;
                return;
            }
        }
        n->handle();
    }
};
