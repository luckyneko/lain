// flowview — the lain::flow inspector. A thin lain::app client: gui-mode opens a
// window (lain::gui panels land in the next step); cli-mode (--headless) evaluates
// the example graph and dumps its output. See WORK.md step 9.

#include "flowviewapp.h"

#include <lain/app/application.h>

int main(int argc, char** argv)
{
	flowview::FlowviewApp delegate;
	lain::app::Application app(delegate);
	return app.run(argc, argv);
}
