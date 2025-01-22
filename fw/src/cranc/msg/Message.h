#pragma once

#include "Listener.h"

#include "cranc/platform/system.h"

#include "cranc/util/LinkedList.h"
#include "cranc/util/function.h"

#include <utility>
#include <optional>

namespace cranc
{

struct MessageBase : cranc::util::LinkedList<MessageBase> {
	
	bool in_use() const {
		cranc::LockGuard lock;
		return not empty();
	}


	void invoke() {
		invoke_(arg_);
	}

protected:
	MessageBase(f_ptr<void(void const*)> invoker, void const* arg) : invoke_{invoker}, arg_{arg}
	{}
	void const* arg_{};
	f_ptr<void(void const*)> invoke_;
};

template<typename T>
struct Message : MessageBase
{
	Message() : MessageBase{invoke_f, this}
	{}

	T& operator*() {
		return *payload;
	}

	T const& operator*() const {
		return *payload;
	}

	T* operator->() {
		return payload.operator->();
	}

	std::optional<T>& get() {
		return payload;
	}

	void post() {
		cranc::LockGuard lock;
		auto& head = cranc::util::GloballyLinkedList<MessageBase>::getHead();
		head.insertBefore(*this);
	}

	void invokeDirectly() {
		invoke();
	}

private:
	std::optional<T> payload;
	static void invoke_f(void const* arg) {
		auto const* tgt = reinterpret_cast<Message<T> const*>(arg);
		assert(tgt->payload);
		auto& listeners = Listener<T>::getHead();

		for (auto& l : listeners) {
			(*l)(*(tgt->payload));
		}
	}
};

template<typename T, std::size_t N>
struct MessageBufferMemory {
	~MessageBufferMemory() {}

	template<typename... Args>
	Message<T>* getFreeMessage(Args &&... args) {
		cranc::LockGuard lock;
		for (auto i{0U}; i < mMessages.size(); ++i) {
			auto &msg = mMessages[i];
			if (not msg.in_use()) {
				msg.get().emplace(std::forward<Args>(args)...);
				return &msg;
			}
		}
		return nullptr;
	}
private:
	std::array<Message<T>, N> mMessages{};
};

}
