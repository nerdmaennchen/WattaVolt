#include "PersistentConfig.h"
#include "util/Hash.h"
#include "util/ChunkBuffer.h"

#include "pico.h"
#include "hardware/flash.h"

#include <algorithm>


namespace
{

struct ConfigDescriptor {
    std::uint32_t id;
    std::uint32_t start_offset;
};

constexpr std::uint32_t flash_config_size = 16*1024;

struct FlashConfig {
    std::uint32_t num_configs;
    union {
        std::array<ConfigDescriptor, flash_config_size/sizeof(ConfigDescriptor) - sizeof(num_configs)> descriptors;
       std::array<std::uint8_t, flash_config_size - sizeof(num_configs)> raw_data;
    };

    std::uint32_t count() const {
        if (num_configs >= raw_data.size() / sizeof(ConfigDescriptor)) {
            return 0;
        }
        return num_configs;
    }
};
static_assert(sizeof(FlashConfig) == flash_config_size);

}

extern "C" {


extern const FlashConfig _flashConfigROM;
extern const std::byte __flash_binary_start; // <- the first address in the flash memory

}

namespace
{

std::uint32_t hash_config(cranc::ApplicationConfigBase const& config) {
    return hash_str(config.getFormat(), hash_str(config.getName()));
}

}

namespace config 
{

PersistentConfigManager::PersistentConfigManager() {
}

bool PersistentConfigManager::load(config::PersistentConfig& p_config) {
    auto hash = hash_config(p_config.config);
    auto flash_cfg = std::launder(&_flashConfigROM);
    for (auto i=0U; i < flash_cfg->count(); ++i) {
        auto const& cfg = flash_cfg->descriptors[i];
        if (cfg.id == hash) {
            if (cfg.start_offset + p_config->config.getSize() < flash_cfg->raw_data.size()) {
                auto s = std::span<const std::uint8_t>{flash_cfg->raw_data.data() + cfg.start_offset, p_config->config.getSize()};
                p_config->config.setValue(s, true);
                return true;
            }
        }
    }
    return false;
}

void PersistentConfigManager::erase() {
    std::size_t offset = reinterpret_cast<std::byte const*>(&_flashConfigROM) - &__flash_binary_start;
    assert(offset % FLASH_SECTOR_SIZE == 0);
    assert(sizeof(_flashConfigROM) % FLASH_PAGE_SIZE == 0);
    cranc::LockGuard lock;
    flash_range_erase(offset, sizeof(_flashConfigROM));
}

void PersistentConfigManager::save() {
    auto& head = cranc::util::GloballyLinkedList<config::PersistentConfig>::getHead();
    std::uint32_t num_configs = 0;
    int config_size = 0;
    
    for (auto& applCfg : head) {
        ++num_configs;
        config_size += applCfg->config.getSize();
    }

    erase();
    std::size_t data_offset = num_configs * sizeof(ConfigDescriptor);
    std::size_t flash_offset = reinterpret_cast<std::byte const*>(&_flashConfigROM) - &__flash_binary_start;
    ChunkBuffer<FLASH_PAGE_SIZE> buffer;

    auto write = [&](std::span<std::uint8_t const> s) {
        while (not s.empty()) {
            buffer.push(s);
            if (buffer.full()) {
                auto chunk = buffer.flush();
                assert(flash_offset % FLASH_PAGE_SIZE == 0);
                assert(chunk.size() == FLASH_PAGE_SIZE);
                cranc::LockGuard lock;
                flash_range_program(flash_offset, chunk.data(), chunk.size());
                flash_offset += chunk.size();
            }
        }
    };

    {
        std::uint32_t c = head.count();
        write(to_span(c));
    }
    for (auto& applCfg : head) {
        ConfigDescriptor descriptor {
            .id           = hash_config(applCfg->config),
            .start_offset = data_offset
        };
        data_offset += applCfg->config.getSize();
        write(to_span(descriptor));
    }
    for (auto& applCfg : head) {
        auto s = applCfg->config.getValue();
        write(s);
    }

    if (not buffer.empty()) {
        write(buffer.payload);
    }

}

PersistentConfig::PersistentConfig(::cranc::ApplicationConfigBase& i_config) : config{i_config} {
    PersistentConfigManager::get().load(*this);
}

}