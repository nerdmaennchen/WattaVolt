#pragma once

#include "cranc/util/function.h"
#include "cranc/util/Claimable.h"
#include <cstdint>

#include <span>

struct I2C : cranc::Claimable<I2C> {
    using CB = cranc::function<void(bool)>;

    void set_addr(std::uint8_t addr, CB cb={});
    void write(std::span<const std::uint8_t> data, bool stop_at_end, CB cb={});
    void read(std::span<std::uint8_t> data, bool stop_at_end, CB cb={});

    void write_cont(std::span<const std::uint8_t> data, bool stop_at_end, CB cb={});
    void read_cont(std::span<std::uint8_t> data, bool stop_at_end, CB cb={});
    
    void sync(CB cb={});
    void recover_bus(CB cb={});
    
    friend cranc::Claimable<I2C>;
private:
    I2C();
};
