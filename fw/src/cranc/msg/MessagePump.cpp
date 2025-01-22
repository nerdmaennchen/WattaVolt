#include "cranc/msg/MessagePump.h"
#include "cranc/platform/system.h"
#include "cranc/util/FiFo.h"

#include "cranc/timer/ISRTime.h"

namespace cranc
{

namespace {

auto& queue = cranc::util::GloballyLinkedList<MessageBase>::getHead();

}

MessageBase* MessagePump::frontMessage() {
	cranc::LockGuard lock;
	if (queue.empty()) {
		return {};
	}
	if (queue.count() > 16) {
		__breakpoint();
	}

	return static_cast<MessageBase*>(queue.next);
}


}
