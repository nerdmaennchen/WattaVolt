#pragma once

#include "Awaitable.h"

#include <coroutine>
#include <optional>

namespace cranc::coro
{

template <typename T>
struct Generator {
    struct promise_type;
    using coro_handle = std::coroutine_handle<promise_type>;

    struct promise_type
    {
        std::optional<T> value_;
        Generator get_return_object()
        {
            return Generator(coro_handle::from_promise(*this));
        }
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void unhandled_exception() { }

        template <std::convertible_to<T> From>
        std::suspend_always yield_value(From&& from)
        {
            value_.emplace(std::forward<From>(from));
            return {};
        }
        void return_void() { }
    };

    coro_handle handle{};

    Generator() = default;
    Generator(coro_handle h)
        : handle(h)
    {
    }
    Generator(Generator&& other) { *this = std::move(other); }
    Generator& operator=(Generator&& rhs) {
        std::swap(handle, rhs.handle);
        return *this;
    }

    ~Generator() {
        if (handle) {
            handle.destroy();
        }
    }

    T get()
    {
        return std::move(*handle.promise().value_);
    }

    bool advance() {
        handle();
        return not handle.done();
    }

    operator bool() const {
        return handle and not handle.done();
    }
};

template <>
struct Generator<void> {
    struct promise_type;
    using coro_handle = std::coroutine_handle<promise_type>;

    struct promise_type
    {
        Generator get_return_object()
        {
            return Generator(coro_handle::from_promise(*this));
        }
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void unhandled_exception() { }


        struct Thingy {};
        template<typename Whatever=Thingy>
        std::suspend_always yield_value(Whatever&&)
        {
            return {};
        }
        void return_void() { }
    };

    coro_handle handle{};

    Generator() = default;
    Generator(coro_handle h)
        : handle(h)
    {
    }
    Generator(Generator&& other) { *this = std::move(other); }
    Generator& operator=(Generator&& rhs) {
        std::swap(handle, rhs.handle);
        return *this;
    }

    ~Generator() {
        if (handle) {
            handle.destroy();
        }
    }

    bool advance() {
        handle();
        return not handle.done();
    }

    operator bool() const {
        return handle and not handle.done();
    }
};


template <typename T, typename LockT=cranc::LockGuard>
struct AsyncGenerator {
    struct promise_type;
    using coro_handle = std::coroutine_handle<promise_type>;

    struct promise_type : Awaitable<bool, LockT>
    {
        std::optional<T> value_;
        AsyncGenerator get_return_object()
        {
            return AsyncGenerator(coro_handle::from_promise(*this));
        }
        
        std::suspend_always initial_suspend() { return {}; }
        auto final_suspend() noexcept {
            struct FinalAwaitable {
                promise_type& promise;
                constexpr bool await_ready() const noexcept { return false; }
                constexpr void await_resume() const noexcept { }
                std::coroutine_handle<> await_suspend(std::coroutine_handle<>) const noexcept
                {
                    [[maybe_unused]] LockT lock;
                    promise.val.emplace(false);
                    if (promise.handle) {
                        return promise.handle;
                    }
                    return noop_handle;
                }
                std::noop_coroutine_handle noop_handle { std::noop_coroutine() };
            };
            return FinalAwaitable { *this };
        }
        void unhandled_exception() { }

        template <std::convertible_to<T> From>
        auto yield_value(From&& from)
        {
            value_.emplace(std::forward<From>(from));
            struct YieldAwaitable {
                promise_type& promise;
                constexpr bool await_ready() const noexcept { return false; }
                constexpr void await_resume() const noexcept { }
                std::coroutine_handle<> await_suspend(std::coroutine_handle<>) const noexcept
                {
                    [[maybe_unused]] LockT lock;
                    promise.val.emplace(true);
                    if (promise.handle) {
                        return promise.handle;
                    }
                    return noop_handle;
                }
                std::noop_coroutine_handle noop_handle { std::noop_coroutine() };
            };
            return YieldAwaitable { *this };
        }
        void return_void() { 
            [[maybe_unused]] LockT lock;
            this->val.emplace(false);
        }
        
        auto await_transform(auto&& awaitable) {
            struct Wrapper {
                decltype(awaitable)& a;
                bool await_ready() { 
                    return a.await_ready();
                }
                auto await_suspend(std::coroutine_handle<> h) {
                    if constexpr (std::is_same_v<decltype(a.await_suspend(h)), void>) {
                        a.await_suspend(h);
                    } else {
                        return a.await_suspend(h);
                    }
                }
                auto await_resume() { 
                    return a.await_resume();
                }

                ~Wrapper() {
                    a.remove_awaiter();
                }
            };
            return Wrapper{awaitable};
        }
    };

    coro_handle handle{};

    AsyncGenerator() = default;
    AsyncGenerator(coro_handle h)
        : handle(h)
    {
    }
    // AsyncGenerator(AsyncGenerator const&) = delete;
    // AsyncGenerator& operator=(AsyncGenerator const&) = delete;

    AsyncGenerator(AsyncGenerator&& other) { *this = std::move(other); }
    AsyncGenerator& operator=(AsyncGenerator&& rhs) {
        std::swap(handle, rhs.handle);
        return *this;
    }

    ~AsyncGenerator() {
        if (handle) {
            LockT lock;
            handle.destroy();
        }
    }

    T get()
    {
        return std::move(*handle.promise().value_);
    }

    Awaitable<bool, LockT>& advance() {
        handle();
        return handle.promise();
    }

    bool done() const {
        return (not handle) or handle.done();
    }
};


}
