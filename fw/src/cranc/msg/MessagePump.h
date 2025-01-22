#pragma once

#include "cranc/module/Module.h"
#include "cranc/util/Singleton.h"
#include "cranc/util/LinkedList.h"

#include "Message.h"

namespace cranc
{

struct MessagePump : cranc::util::Singleton<MessagePump>
{
	MessageBase* frontMessage();
};

} /* namespace cranc */
