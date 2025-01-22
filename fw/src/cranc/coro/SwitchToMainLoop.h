#pragma once

#include <cranc/util/function.h>
#include "cranc/platform/system.h"
#include "cranc/msg/Message.h"

#include <coroutine>
#include <optional>
#include <cassert>

namespace cranc::coro
{

struct SwitchToMainLoop {
    bool await_ready();
    
    void await_suspend(std::coroutine_handle<> h) {
        assert(not msg);
        assert(h);
        msg = msgBuf.getFreeMessage(h);
        assert(msg);
        msg->post();
    }

    void await_resume() { (*msg)->handle = nullptr; msg = nullptr;}

    void remove_awaiter() {
        await_resume();
    }

    SwitchToMainLoop() = default;
    SwitchToMainLoop(SwitchToMainLoop const&) = delete;
    SwitchToMainLoop(SwitchToMainLoop&&) = delete;
    SwitchToMainLoop& operator=(SwitchToMainLoop const&) = delete;
    SwitchToMainLoop& operator=(SwitchToMainLoop&&) = delete;

    struct SwitchMsg{
        std::coroutine_handle<> handle{};
    };

private:
    Message<SwitchMsg>* msg{};
    inline static cranc::MessageBufferMemory<SwitchToMainLoop::SwitchMsg, 64> msgBuf;
};

}
