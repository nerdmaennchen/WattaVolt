#pragma once

#include <array>
#include <atomic>

#include <cassert>

namespace cranc
{

template<typename T, std::size_t N, typename Lock = int>
struct FIFO {
	template<typename... Args>
	std::size_t put(Args &&... args) {
		[[maybe_unused]] Lock lock;
		if (mCount < N) {
			new (&mStorage.arr[mEnd]) T{std::forward<Args>(args)...};
			mEnd = (mEnd + 1) % N;
			++mCount;
			return 1;
		}
		return 0;
	}

	std::size_t put(T const* _vals, std::size_t cnt) {
		[[maybe_unused]] Lock lock;
		std::size_t p = 0;
		while (p < cnt && put(_vals[p])) {
			++p;
		}
		return p;
	}

	void pop(std::size_t cnt) {
		[[maybe_unused]] Lock lock;
		assert(cnt <= mCount);
		while (cnt) {
			mStorage.arr[mStart].~T();
			mStart = (mStart + 1) % N;
			mCount -= 1;
			cnt -= 1;
		}
	}

	void clear() {
		[[maybe_unused]] Lock lock;
		mStart = 0;
		mEnd = 0;
		mCount = 0;
	}

	T& operator[](std::size_t idx) {
		idx = (mStart + idx) % N;
		return mStorage.arr[idx];
	}

	std::size_t countFree() const {
		return N - mCount;
	}

	std::size_t count() const {
		return mCount;
	}

	constexpr std::size_t capacity() {
		return N;
	}

private:
	std::size_t mCount {0}, mStart {0}, mEnd {0};
	struct empty_type{};

	union StorageT {
		std::array<T, N> arr;
		empty_type empty;
		StorageT() : empty{} {};
		~StorageT() { }
	};
	StorageT mStorage;
};

}
