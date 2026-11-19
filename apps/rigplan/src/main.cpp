// rigplan — rig design (WORK.md M15): the editor, and `render`, which writes a rig plan's synthetic
// capture headless.

#include "rigplanapp.h"

#include <lain/app/application.h>

int main(int argc, char** argv)
{
	rigplan::RigplanApp delegate;
	lain::app::Application app(delegate, {"rigplan", {0, 1, 0}});

	// The three phases, in order. A value from initialise is an answer rather than a failure:
	// --version, --licenses and --help all take that path, as does a command line it refused.
	if (const auto code = app.initialise(argc, argv))
		return *code;

	const int status = app.run();
	app.shutdown();
	return status;
}
