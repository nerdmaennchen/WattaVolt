#pragma once

#include "cranc/util/function.h"
#include "cranc/util/LinkedList.h"

#include <span>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

namespace cranc
{

struct ApplicationConfigBase : cranc::util::GloballyLinkedList<ApplicationConfigBase> {
	virtual ~ApplicationConfigBase() {}

	virtual std::string_view getName() const = 0;
	virtual std::string_view getFormat() const = 0;

	virtual void setValue(std::span<const std::uint8_t>, bool quiet = false) = 0;
	virtual std::span<const std::uint8_t> getValue() = 0;
	virtual std::size_t getSize() const = 0;
};

template<typename T>
struct ApplicationConfig : ApplicationConfigBase
{
private:
	std::string_view name;
	std::string_view format;
	T value;
	using callbackType = cranc::function<void(bool)>;
	callbackType mCB;
public:

	ApplicationConfig(std::string_view _name, std::string_view _format) : name(_name), format(_format)  {}
	ApplicationConfig(std::string_view _name, std::string_view _format, callbackType cb) : name(_name), format(_format), mCB{std::move(cb)} {}
	ApplicationConfig(std::string_view _name, std::string_view _format, T const& val) : name(_name), format(_format), value(val) {}
	ApplicationConfig(std::string_view _name, std::string_view _format, callbackType cb, T const& val) : name(_name), format(_format), value(val), mCB{std::move(cb)} {}

	std::string_view getName() const final { return name; }
	std::string_view getFormat() const final { return format; }

	void setValue(std::span<const std::uint8_t> d, bool quiet) final {
		assert(d.size() == sizeof(T));
		std::memcpy(&value, d.data(), d.size());
		if (not quiet) {
			mCB(true);
		}
	}

	std::span<const std::uint8_t> getValue() final {
		mCB(false);
		return {reinterpret_cast<std::uint8_t const*>(&value), getSize()};
	}
	std::size_t getSize() const final {
		return sizeof(T);
	}

	void set(T const& val) {
		value = val;
		mCB(true);
	}

	T const& get() {
		mCB(false);
		return value;
	}


    T& operator*() { return value; }
    T const& operator*() const { return value; }
	T* operator->() { return &value; }
	T const* operator->() const { return &value; }
};

template<typename T>
struct ApplicationConfigMapping : ApplicationConfigBase
{
private:
	std::string_view name;
	std::string_view format;
	T& value;
	using callbackType = cranc::function<void(bool)>;
	callbackType mCB;
public:

	ApplicationConfigMapping(std::string_view _name, std::string_view _format, T& map) : name(_name), format(_format), value(map) {}
	ApplicationConfigMapping(std::string_view _name, std::string_view _format, callbackType cb, T& map) : name(_name), format(_format), value(map), mCB{std::move(cb)} {}

	std::string_view getName() const final { return name; }
	std::string_view getFormat() const final { return format; }

	void setValue(std::span<const std::uint8_t> d, bool quiet) final {
		assert(d.size() == sizeof(T));
		std::memcpy(&value, d.data(), d.size());
		if (not quiet) {
			mCB(true);
		}
	}

	std::span<const std::uint8_t> getValue() final {
		mCB(false);
		return {reinterpret_cast<std::uint8_t const*>(&value), sizeof(T)};
	}
	std::size_t getSize() const final {
		return sizeof(T);
	}


    T& operator*() { return value; }
    T const& operator*() const { return value; }
	T* operator->() { return &value; }
	T const* operator->() const { return &value; }
};

template<typename T>
ApplicationConfigMapping(std::string_view, std::string_view, T&) -> ApplicationConfigMapping<T>;

template<>
struct ApplicationConfig<void> : ApplicationConfigBase
{
private:
	std::string_view name;
	std::string_view format;
	using callbackType = cranc::function<void()>;
	callbackType mCB;
public:

	ApplicationConfig(std::string_view _name) : name(_name), format("") {}
	ApplicationConfig(std::string_view _name, callbackType cb) : name(_name), format(""), mCB{std::move(cb)} {}

	std::string_view getName() const override { return name; }
	std::string_view getFormat() const override { return format; }
	std::size_t getSize() const final {
		return 0;
	}

	void setValue(std::span<const std::uint8_t> d, bool quiet) final {
		if (not quiet) {
			mCB();
		}
	}

	std::span<const std::uint8_t> getValue() final { return {}; }
};

} /* namespace cranc */

