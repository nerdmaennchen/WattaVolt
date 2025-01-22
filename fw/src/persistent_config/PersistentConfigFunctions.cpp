#include "PersistentConfig.h"

#include <cranc/config/ApplicationConfig.h>

namespace {


cranc::ApplicationConfig<void> save_conf{"config.save", [] { config::PersistentConfigManager::get().save(); }};
cranc::ApplicationConfig<void> erase_conf{"config.erase",[] { config::PersistentConfigManager::get().erase(); }};

}