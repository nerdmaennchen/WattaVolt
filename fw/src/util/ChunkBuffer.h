#pragma once

#include "span_helpers.h"

#include <array>
#include <span>
#include <cstdint>
#include <cstring>

template<std::size_t chunk_size>
struct ChunkBuffer {
	std::array<std::uint8_t, chunk_size> payload{};
	std::size_t filled{};
	std::size_t remaining{payload.size()};

	ChunkBuffer() {
		reset();
	}

	void reset() {
		filled = 0;
		remaining = payload.size();
	}

	bool full() const {
		return filled == payload.size();
	}

	bool empty() const {
		return filled == 0;
	}

	std::span<std::uint8_t> flush() {
		auto old_filled = filled;
		reset();
		return {payload.data(), old_filled};
	}

	std::size_t push(std::span<std::uint8_t const>& data) {
		auto to_consume = trim_span(data, remaining);
		std::memcpy(payload.data()+filled, to_consume.data(), to_consume.size());
		filled += to_consume.size();
		remaining -= to_consume.size();
		data = data.subspan(to_consume.size());
		return to_consume.size();
	}
};