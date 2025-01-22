#pragma once
#include <coroutine>

#include "Awaitable.h"

#include <utility>

namespace cranc::coro {

namespace detail {

template <typename TaskType, typename ReturnType, typename LockT>
struct promise_type : Awaitable<ReturnType, LockT> {
    TaskType get_return_object() { return TaskType { std::coroutine_handle<promise_type>::from_promise(*this) }; }
    std::suspend_never initial_suspend() { return {}; }
    auto final_suspend() noexcept
    {
        struct FinalAwaitable {
            promise_type& promise;
            constexpr bool await_ready() const noexcept { return false; }
            constexpr void await_resume() const noexcept { }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<>) const noexcept
            {
                [[maybe_unused]] LockT lock;
                if (promise.handle) {
                    return promise.handle;
                }
                return noop_handle;
            }
            std::noop_coroutine_handle noop_handle { std::noop_coroutine() };
        };
        return FinalAwaitable { *this };
    }
    ReturnType return_object() { return {}; }
    void unhandled_exception() { throw; }
    void return_value(auto&& expr)
    {
        [[maybe_unused]] LockT lock;
        this->val.emplace(std::forward<decltype(expr)>(expr));
    }
};

template <typename TaskType, typename LockT>
struct promise_type<TaskType, void, LockT> : Awaitable<void, LockT> {
    TaskType get_return_object() { return TaskType { std::coroutine_handle<promise_type>::from_promise(*this) }; }
    std::suspend_never initial_suspend() { return {}; }
    auto final_suspend() noexcept
    {
        struct FinalAwaitable {
            promise_type& promise;
            constexpr bool await_ready() const noexcept { return false; }
            constexpr void await_resume() const noexcept { }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<>) const noexcept
            {
                [[maybe_unused]] LockT lock;
                if (promise.handle) {
                    return promise.handle;
                }
                return noop_handle;
            }
            std::noop_coroutine_handle noop_handle { std::noop_coroutine() };
        };
        return FinalAwaitable { *this };
    }
    void return_void()
    {
        [[maybe_unused]] LockT lock;
        this->has_value = true;
    }
    void unhandled_exception() { throw; }

    template<typename T>
    auto await_transform(Awaitable<T>&& awaitable) {
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

    template<typename T>
    auto await_transform(T&& awaitable) -> decltype(awaitable) {
        return awaitable;
    }
};

}

template <typename T = void, typename LockT=cranc::LockGuard>
struct Task {
    using promise_type = detail::promise_type<Task, T, LockT>;
    using coro_handle = std::coroutine_handle<promise_type>;

    coro_handle handle;

    Task() = default;
    Task(coro_handle i_handle)
        : handle { i_handle }
    {
    }
    Task(Task&& rhs) noexcept { *this = std::move(rhs); };
    Task& operator=(Task&& rhs) noexcept
    {
        std::swap(handle, rhs.handle);
        return *this;
    };

    ~Task()
    {
        if (handle) {
            handle.destroy();
        }
    }

    void terminate() {
        if (handle) {
            LockT lock;
            handle.destroy();
            handle = nullptr;
        }
    }

    bool done() const {
        return (not handle) or handle.done();
    }

    bool await_ready()
    {
        return handle.promise().await_ready();
    }

    void await_suspend(std::coroutine_handle<> h)
    {
        handle.promise().await_suspend(h);
    }

    auto await_resume()
    {
        return handle.promise().await_resume();
    }

    void remove_awaiter() {
        handle.promise().remove_awaiter();
    }
};

struct FAFTask {
    struct promise_type {
        FAFTask get_return_object() { return FAFTask{}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };
};

}
