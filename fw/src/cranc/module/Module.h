#pragma once

#include "cranc/util/LinkedList.h"

namespace cranc
{

struct Module : cranc::util::GloballySortedLinkedList<Module> {
	using cranc::util::GloballySortedLinkedList<Module>::GloballySortedLinkedList;
	virtual void init() {};
};

void InitializeModules();

}

