#include "appcontext.h"

#include <lain/gui/dock.h>	// activateWindowTab (bring the Graph tab forward)
#include <lain/gui/nodes.h> // imnodes selection

namespace flowview
{
	using namespace lain;

	void AppContext::locateNode(flow::NodeId id)
	{
		gui::nodes::ClearNodeSelection();
		gui::nodes::SelectNode(static_cast<int>(id.value()));
		locateTarget = id;				 // centred on the next Graph draw (needs the node's drawn size)
		gui::activateWindowTab("Graph"); // bring the canvas forward so the located node is visible
	}
} // namespace flowview
