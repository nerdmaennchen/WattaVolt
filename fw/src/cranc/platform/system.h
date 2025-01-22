#pragma once

#include <hardware/sync.h>

/*
 * here are the most important platform dependant functions listed.
 * The framework cannot work correctly if those functions are not implemented correctly!
 */


/*
 * mutex functions (eg. suspend all interrupts)
 * The mutex functions must be safe for recursive usage!
 */
void system_reset(void);

namespace cranc
{

struct [[maybe_unused]] LockGuard final {
	LockGuard() {
		int_state = save_and_disable_interrupts();
	}
	~LockGuard() {
			restore_interrupts_from_disabled(int_state);
	}
private:
	int int_state = 0;
};

}
