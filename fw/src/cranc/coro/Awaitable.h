#pragma once

#include "cranc/util/function.h"
#include "cranc/platform/system.h"
#include "cranc/util/Claimable.h"
#include "cranc/util/FiFo.h"
#include "cranc/util/Finally.h"
#include "cranc/timer/swTimer.h"

#include <coroutine>
#include <optional>
#include <source_location>

namespace cranc::coro
{

template<typename T, typename LockT=int>
struct Awaitable {
    std::coroutine_handle<> handle{};
    std::optional<T> val;
    
    Awaitable() = default;
    Awaitable(Awaitable const&) = delete;
    Awaitable(Awaitable &&) = delete;
    Awaitable& operator=(Awaitable const&) = delete;
    Awaitable& operator=(Awaitable &&) = delete;

    std::source_location loc;

    bool await_ready() { 
        [[maybe_unused]] LockT lock; 
        return val.has_value(); 
    }

    bool await_suspend(std::coroutine_handle<> h, std::source_location l = std::source_location::current())
    {
        loc = l;
        [[maybe_unused]] LockT lock;
        if (val.has_value()) {
            return false;
        }
        handle = h;
        return true;
    }

    T await_resume() { 
        [[maybe_unused]] LockT lock;
        handle = nullptr;
        auto reset = Finally{[this]{
            val.reset();
        }};
        if constexpr (not std::is_reference_v<T>) {
            return std::move(val.value());
        } else {
            return val.value();
        }
    }

    template<typename... Args> 
    void operator()(Args&&... v)
    {
        std::coroutine_handle<> h{};
        {
            [[maybe_unused]] LockT lock;
            val.emplace(std::forward<Args>(v)...);
            std::swap(h, handle);
        }
        if (h) {
            h.resume();
        }
    }

    void operator()(T v)
    {
        std::coroutine_handle<> h{};
        {
            [[maybe_unused]] LockT lock;
            val.emplace(std::move(v));
            std::swap(h, handle);
        }
        if (h) {
            h.resume();
        }
    }


    void clear() {
        [[maybe_unused]] LockT lock;
        val.reset();
        handle = nullptr;
    }

    void remove_awaiter() {
        [[maybe_unused]] LockT lock;
        handle = nullptr;
    }
};

template<typename LockT>
struct Awaitable<void, LockT> {
    std::coroutine_handle<> handle;
    bool has_value{false};

    Awaitable() = default;

    Awaitable(Awaitable const&) = delete;
    Awaitable(Awaitable &&) = delete;
    Awaitable& operator=(Awaitable const&) = delete;
    Awaitable& operator=(Awaitable &&) = delete;

    bool await_ready() { 
        [[maybe_unused]] LockT lock;
        return has_value; 
    }

    bool await_suspend(std::coroutine_handle<> h)
    {
        [[maybe_unused]] LockT lock;
        if (has_value) {
            return false;
        } else {
            handle = h;
        }
        return true;
    }

    void await_resume() {
        [[maybe_unused]] LockT lock;
        handle = nullptr; 
        has_value = false;
    }

    void operator()()
    {
        std::coroutine_handle<> h{};
        {
            [[maybe_unused]] LockT lock;
            if (handle) {
                std::swap(h, handle);
            } else {
                has_value = true;
            }
        }
        if (h) {
            h.resume();
        }
    }

    void clear() {
        [[maybe_unused]] LockT lock;
        has_value = false;
        handle = nullptr;
    }
    
    void remove_awaiter() {
        [[maybe_unused]] LockT lock;
        handle = nullptr;
    }
};


template<typename T, std::size_t N, typename LockT=int>
struct QueuedAwaitable {
    std::coroutine_handle<> handle;
    FIFO<T, N, LockT> queue;
    
    QueuedAwaitable() = default;
    QueuedAwaitable(QueuedAwaitable const&) = delete;
    QueuedAwaitable(QueuedAwaitable &&) = delete;
    QueuedAwaitable& operator=(QueuedAwaitable const&) = delete;
    QueuedAwaitable& operator=(QueuedAwaitable &&) = delete;


    bool await_ready() const { 
        return queue.count(); 
    }

    bool await_suspend(std::coroutine_handle<> h)
    {
        [[maybe_unused]] LockT lock;
        if (await_ready()) {
            return false;
        }
        handle = h;
        return true;
    }

    T await_resume() { 
        [[maybe_unused]] LockT lock;
        handle = nullptr;
        T t = std::move(queue[0]);
        queue.pop(1);
        return t;
    }

    template<typename... Args> 
    std::size_t operator()(Args&&... v)
    {
        std::coroutine_handle<> h{};
        std::size_t success {};
        {
            [[maybe_unused]] LockT lock;
            success = queue.put(std::forward<Args>(v)...);
            h = handle;
        }
        if (h) {
            h.resume();
        }
        return success;
    }

    std::size_t operator()(T v)
    {
        std::coroutine_handle<> h{};
        std::size_t success {};
        {
            [[maybe_unused]] LockT lock;
            success = queue.put(std::move(v));
            h = handle;
        }
        if (h) {
            h.resume();
        }
        return success;
    }


    void clear() {
        [[maybe_unused]] LockT lock;
        queue.clear();
        handle = nullptr;
    }

    void remove_awaiter() {
        [[maybe_unused]] LockT lock;
        handle = nullptr;
    }
};

template<std::size_t N, typename LockT>
struct QueuedAwaitable<void, N, LockT> {
    std::coroutine_handle<> handle;
    std::size_t queue{};
    
    QueuedAwaitable() = default;
    QueuedAwaitable(QueuedAwaitable const&) = delete;
    QueuedAwaitable(QueuedAwaitable &&) = delete;
    QueuedAwaitable& operator=(QueuedAwaitable const&) = delete;
    QueuedAwaitable& operator=(QueuedAwaitable &&) = delete;


    bool await_ready() const { 
        [[maybe_unused]] LockT lock;
        return queue; 
    }

    bool await_suspend(std::coroutine_handle<> h)
    {
        [[maybe_unused]] LockT lock;
        if (await_ready()) {
            return false;
        }
        handle = h;
        return true;
    }

    void await_resume() { 
        [[maybe_unused]] LockT lock;
        handle = nullptr;
        --queue;
    }

    void operator()()
    {
        std::coroutine_handle<> h{};
        {
            [[maybe_unused]] LockT lock;
            h = handle;
            ++queue;
        }
        if (h) {
            h.resume();
        }
    }

    void clear() {
        [[maybe_unused]] LockT lock;
        queue = 0;
        handle = nullptr;
    }

    void remove_awaiter() {
        [[maybe_unused]] LockT lock;
        handle = nullptr;
    }
};


template<typename T, typename LockT=cranc::LockGuard>
struct AwaitableClaim : T::Claim, Awaitable<cranc::Access<T>, LockT> {
    using T::Claim::Claim;
    using Access = cranc::Access<T>;

    AwaitableClaim(int priority = std::numeric_limits<int>::max())
    : T::Claim{[this](Access a){Awaitable<cranc::Access<T>, LockT>::operator()(std::move(a));}, priority}
    {}
};

template<typename LockT=cranc::LockGuard>
struct AwaitableDelay : Awaitable<int, LockT> {
    AwaitableDelay() = default;

    AwaitableDelay(cranc::TimerInterval interval) {
        timer.start(cranc::getSystemTime() + interval);
    }
    
    AwaitableDelay(TimerInterval timeout, TimerInterval delay) {
        timer.start(timeout, delay);
    }

    void start(TimerInterval timeout, TimerInterval delay=TimerInterval::zero()) {
        timer.start(timeout,  delay);
    }
    
    void stop() {
        timer.stop();
    }

    AwaitableDelay(AwaitableDelay const&) = delete;
    AwaitableDelay& operator=(AwaitableDelay const&) = delete;
    AwaitableDelay(AwaitableDelay&&) = delete;
    AwaitableDelay& operator=(AwaitableDelay&&) = delete;

private:
    cranc::Timer timer{[this](int elapses){
        [[maybe_unused]] LockT lock;
        (*this)(this->val.value_or(0) + elapses);
    }};
};

}
