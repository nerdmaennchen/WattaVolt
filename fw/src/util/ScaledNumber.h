#pragma once

#include <ratio>
#include <type_traits>
#include <cstdint>


namespace detail {

template<typename T>
struct is_ratio : std::false_type {};

template<std::intmax_t Num, std::intmax_t Denom>
struct is_ratio<std::ratio<Num, Denom>> : std::true_type {};

template<typename T>
inline constexpr bool is_ratio_v = is_ratio<T>::value;

}

template<typename T, typename ratioT=std::ratio<1, 1>> requires (std::is_integral_v<T> and detail::is_ratio_v<ratioT>)
struct ScaledNumber {
    using ratio = ratioT;
    using value_type = T;
    value_type val {};

    ScaledNumber() = default;
    ScaledNumber(value_type v) : val{v} {}
    ScaledNumber(ScaledNumber&) = default;
    ScaledNumber(ScaledNumber&&) = default;
    ScaledNumber& operator=(ScaledNumber&) = default;
    ScaledNumber& operator=(ScaledNumber&&) = default;

    template<typename T_from, typename ratio_from>
    ScaledNumber(ScaledNumber<T_from, ratio_from> const& v) : val{scaled_number_cast<ScaledNumber>(v).val} {}

    auto operator<=>(ScaledNumber const&) const = default;
};


namespace detail {

template<typename T>
struct is_scaled_number : std::false_type {};
template<typename T, typename ratioT> //requires (is_ratio_v<ratioT>)
struct is_scaled_number<ScaledNumber<T, ratioT>> : std::true_type {};

template<typename T>
inline constexpr bool is_scaled_number_v = is_scaled_number<T>::value;

}

template<typename To, typename T_from, typename ratio_from> requires (detail::is_scaled_number<To>::value)
To scaled_number_cast(ScaledNumber<T_from,  ratio_from> const& v) {
    using conv = std::ratio_divide<ratio_from, typename To::ratio>;
    return To{static_cast<typename To::value_type>(v.val * conv::num / conv::den)};
}
