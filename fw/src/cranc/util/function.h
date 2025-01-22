#pragma once

#include <functional>

namespace cranc
{

namespace detail {

template<typename FTor>
struct function_helper;

template<typename Res, typename... Args>
struct function_helper<Res(Args...)> {
    using f_type = Res (*)(Args...);
};


}
template<typename Ftor>
using f_ptr = detail::function_helper<Ftor>::f_type;


template<typename T>
struct function;
template<typename Res, typename... Args>
struct function<Res(Args...)> {
private:
    using CaptureBufT = std::array<void*, 2>;
    CaptureBufT capture_buffer{};
    f_ptr<Res(void const*, Args...)> t_ptr{};

public:
    template<typename Functor> requires (sizeof(Functor) <= sizeof(CaptureBufT))
    constexpr function(Functor functor) {
        t_ptr = [](void const* ftor, Args... args) -> Res {
            return (*reinterpret_cast<Functor const*>(ftor))(std::forward<Args>(args)...);
        };
        
        new (capture_buffer.data()) Functor{std::move(functor)};
    }
    
    constexpr function() = default;
    constexpr function(function&& rhs) {
        *this = std::move(rhs);
    }
    constexpr function(function const&) = default;
    constexpr function& operator=(function const&) = default;

    constexpr function& operator=(function && rhs) {
        std::swap(capture_buffer, rhs.capture_buffer);
        std::swap(t_ptr, rhs.t_ptr);
        return *this;
    }

    constexpr Res operator()(Args... args) const {
        if (t_ptr) {
			if constexpr (std::is_same_v<Res, void>) {
				(*t_ptr)(capture_buffer.data(), std::forward<Args>(args)...);
			} else {
            	return (*t_ptr)(capture_buffer.data(), std::forward<Args>(args)...);
			}
        }
		if constexpr (not std::is_same_v<Res, void>) {
			return {};
		}
    }

    constexpr operator bool() const {
        return bool(t_ptr);
    }

};

}
