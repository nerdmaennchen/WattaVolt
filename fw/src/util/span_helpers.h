#pragma once

#include <span>
#include <cstdint>


using span  = std::span<std::uint8_t>;
using spanc = std::span<std::uint8_t const>;

template<typename SpantT = std::uint8_t, typename T>
std::span<SpantT> to_span(T& t) {
	return std::span<SpantT>(reinterpret_cast<SpantT*>(&t), sizeof(t));
}

template<typename SpantT = const std::uint8_t, typename T>
std::span<SpantT> to_span_c(T& t) {
	return std::span<SpantT>(reinterpret_cast<SpantT*>(&t), sizeof(t));
}

template<typename T>
std::span<T> trim_span(std::span<T> s, std::size_t max_size) {
	return s.subspan(0, std::min(s.size(), max_size));
}