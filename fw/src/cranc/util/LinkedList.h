#pragma once

#include <numeric>
#include <cassert>

#include "cranc/platform/system.h"

namespace cranc
{
namespace util
{

template<typename T>
struct LinkedList {
// protected:
	LinkedList* prev {this};
	LinkedList* next {this};
public:
	LinkedList() = default;
	LinkedList(LinkedList const&) = delete;
	LinkedList(LinkedList &&other) { *this = std::move(other); };
	LinkedList& operator=(LinkedList const&) = delete;
	LinkedList& operator=(LinkedList && rhs) {
		cranc::LockGuard lock;
		prev = rhs.prev;
		next = rhs.next;
		rhs.prev = &rhs;
		rhs.next = &rhs;
		next->prev = this;
		prev->next = this;
	};
	~LinkedList() {
		remove();
	}

	void insertNext(LinkedList* other) {
		cranc::LockGuard lock;
		next->prev = other;
		other->next = next;
		other->prev = this;
		next = other;
	}
	void insertBefore(LinkedList* other) {
		cranc::LockGuard lock;
		prev->next = other;
		other->next = this;
		other->prev = prev;
		prev = other;
	}

	void remove() {
		cranc::LockGuard lock;
		prev->next = next;
		next->prev = prev;
		prev = this;
		next = this;
	}

	bool empty() const {
		cranc::LockGuard lock;
		return (next == this) and (prev == this);
	}

	std::size_t count() const {
		std::size_t c{};
		auto const* n = next;
		while (n != this) {
			n = n->next;
			++c;
		}
		return c;
	}

	T* operator->() {
		T* cast =  static_cast<T*>(this);
		return cast;
	}

	T& operator*() {
		T* cast =  static_cast<T*>(this);
		return *cast;
	}

	T const& operator*() const {
		T const* cast =  static_cast<T const*>(this);
		return *cast;
	}

	operator T*() {
		return static_cast<T*>(this);
	}

	struct EndIterator {
		LinkedList<T>* element;

		EndIterator(LinkedList<T>* _element)
			: element{_element}
		{
		}

		auto operator==(auto const& rhs) const {
			return element == rhs.element;
		};

		LinkedList<T>& operator*() const {
			return *element;
		}
		LinkedList<T>* operator->() const {
			return element;
		}
	};

	struct Iterator {
		LinkedList<T>* element;
		LinkedList<T> sentinel;

		Iterator(LinkedList<T>* _element)
			: element{_element}
		{
			element->insertNext(&sentinel);
		}
		Iterator& operator++() {
			element = sentinel->next;
			sentinel.remove();
			element->insertNext(&sentinel);
			return *this;
		}

		auto operator==(auto const& rhs) const {
			return element == rhs.element;
		};

		LinkedList<T>& operator*() const {
			return *element;
		}
		LinkedList<T>* operator->() const {
			return element;
		}
	};

	Iterator begin() {
		return Iterator(this->next);
	}
	EndIterator end() {
		return EndIterator(this);
	}
};

template<typename T>
struct SortedLinkedList : LinkedList<T> {
	SortedLinkedList(int priority) : LinkedList<T>(), mPriority(priority){}

	void insert(SortedLinkedList<T>& l) {
		LinkedList<T>* tgt = this;
		for (auto& element : *this) {
			if (element->mPriority > l.mPriority) {
				tgt = *element;
				break;
			}
		}
		tgt->insertBefore(l);
	}

private:
	const int mPriority;
};

template<typename T>
struct GloballyLinkedList : LinkedList<T> {
	static LinkedList<T>& getHead() {
		static LinkedList<T> instance{};
		return instance;
	}
	GloballyLinkedList() : LinkedList<T>() {
		getHead().insertBefore(this);
	}
};

template<typename T>
struct GloballySortedLinkedList : SortedLinkedList<T> {
	static SortedLinkedList<T>& getHead() {
		static SortedLinkedList<T> instance{std::numeric_limits<int>::max()};
		return instance;
	}

	GloballySortedLinkedList(int priority) : SortedLinkedList<T>{priority} {
		getHead().insert(*this);
	}
};


}
}
