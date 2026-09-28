#pragma once

// lain::task — an ALIAS for multi, not a wrapper over it.
//
// multi is lain's OWN library (a submodule, like archimedes), so there is no upstream
// vocabulary to keep out: lain does not wrap what it owns, and acm:: already appears raw in
// the public headers of lain::app and lain::gui. This file used to hold a Flow / Task /
// Executor trio that renamed multi::Recipe / Step / Context and added nothing a caller could
// not write; a second spelling of one thing is what this repo deletes (see the EvalPath /
// GraphPath note in WORK.md).
//
// A namespace alias cannot be reopened, so lain::task can hold no name of its own. That is the
// point rather than a limitation — it is what forced every behaviour the old wrapper carried to
// find a real owner. See docs/adr/0024-one-process-task-pool.md.
//
// The pool is the PROCESS-GLOBAL one: task::start / stop / parallel / each / range / async
// dispatch onto it with no object in any signature, and lain::app owns its lifetime plus the
// --threads option. An unstarted pool runs every dispatch INLINE on the caller, in dependency
// order, so this header is usable before anything has started a thread.

#include <multi/multi.h>

namespace lain
{
	namespace task = multi;
}
