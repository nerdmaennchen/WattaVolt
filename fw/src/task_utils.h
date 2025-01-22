#pragma once

#include "cranc/coro/Task.h"
#include "cranc/coro/SwitchToMainLoop.h"

template<typename T>
cranc::coro::Task<void> run_task_for(cranc::coro::Task<T> task, cranc::TimerInterval duration) {
    auto timeout = cranc::coro::AwaitableDelay{duration};
    co_await timeout;
    co_await cranc::coro::SwitchToMainLoop{};
    task.terminate();
}