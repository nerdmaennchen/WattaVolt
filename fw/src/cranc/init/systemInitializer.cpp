#include "cranc/module/Module.h"

#include <algorithm>

void cranc::InitializeModules()
{
	auto& modules = cranc::util::GloballySortedLinkedList<Module>::getHead();
	for (auto& module : modules) {
		module->init();
	}
}


