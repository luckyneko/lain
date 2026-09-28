// flowview — the lain::flow inspector. A thin lain::app client: gui-mode opens a window with
// the node canvas and its panels; the run / list subcommands evaluate a graph headless.

#include "flowviewapp.h"

#include <lain/app/application.h>

int main(int argc, char** argv)
{
	flowview::FlowviewApp delegate;
	lain::app::Application app(delegate, {"flowview", {0, 1, 0}});

	// The three phases, in order. A value from initialise is an answer rather than a failure —
	// --version, --licenses and --help all take that path, as does a command line it refused.
	if (const auto code = app.initialise(argc, argv))
		return *code;

	const int status = app.run();
	app.shutdown();
	return status;
}
