#pragma once

#include "cranc/util/Singleton.h"
#include "cranc/util/function.h"
#include "cranc/util/LinkedList.h"

#include <numeric>
#include <span>
#include <cstdint>
#include <utility>

namespace cranc
{

template<typename Resource>
struct Access {

    template<typename FTor>
    Access(Resource& resource, FTor&& releaseCB) 
        : mResource{&resource}
        , mReleaseCB{std::forward<FTor>(releaseCB)} 
    {}

    Access(Access const&) = delete;
    Access& operator=(Access const&) = delete;

    Access(Access&& o) {
        *this = std::move(o);
    }

    Access& operator=(Access&& o) {
        std::swap(mReleaseCB, o.mReleaseCB);
        std::swap(mResource, o.mResource);
        return *this;
    }

    ~Access() {
        mReleaseCB();
    }

    Resource* operator->() {
        return mResource;
    }
    Resource& operator*() {
        return *mResource;
    }
private:
    Resource* mResource{};
    cranc::function<void()> mReleaseCB{};
};


template<typename T>
struct Claimable : private cranc::util::Singleton<Claimable<T>> {

    struct Claim : cranc::util::SortedLinkedList<Claim> {
        template<typename FTor>
        Claim(FTor&& cb, int priority = std::numeric_limits<int>::max())
            : cranc::util::SortedLinkedList<Claim>{priority} 
            , mCB{std::move(cb)}
        {}

        void operator()(Access<T> access) {
            mCB(std::move(access));
        }

    private:
        cranc::function<void(Access<T>)> mCB;
    };

    static void claim(Claim& c) {
        cranc::LockGuard lock;
        auto& wl = waitList();
        bool need_wait = not wl.empty();
        wl.insert(c);

        if (need_wait) {
            return;
        }
        notify_head();
    }

private:

    static void notify_head() {
        cranc::LockGuard lock;
        auto& wl = waitList();
        if (wl.empty()) {
            return;
        }
        auto* next = wl.next;
        static T instance;
        (**next)({instance, [next] {
            next->remove();
            notify_head();
        }});
    } 

    static cranc::util::SortedLinkedList<Claim>& waitList(){
        static cranc::util::SortedLinkedList<Claim> claims{std::numeric_limits<int>::max()};
        return claims;
    }
};

}
