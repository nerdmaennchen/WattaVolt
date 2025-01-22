#pragma once

#include "cranc/util/LinkedList.h"
#include "cranc/util/function.h"
#include "cranc/util/Singleton.h"

namespace cranc
{

template<typename T>
struct Listener : cranc::util::GloballyLinkedList<Listener<T>>
{
	template<typename FTor>
	Listener(FTor&& cb) : mCB{std::forward<FTor>(cb)} {
	}

	void operator()(T const& msg) {
		mCB(msg);
	}
private:
	cranc::function<void(T const&)> mCB;
};

}

