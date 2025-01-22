#pragma once

#include "cranc/config/ApplicationConfig.h"
#include "cranc/util/LinkedList.h"
#include "cranc/util/Claimable.h"

namespace config
{

struct PersistentConfig : cranc::util::GloballyLinkedList<PersistentConfig> {
    cranc::ApplicationConfigBase& config;
    PersistentConfig(cranc::ApplicationConfigBase& i_config);
};

struct PersistentConfigManager : cranc::util::Singleton<PersistentConfigManager> {
    PersistentConfigManager();
    
    bool load(config::PersistentConfig& p_config);
    void save();
    void erase();
};

}