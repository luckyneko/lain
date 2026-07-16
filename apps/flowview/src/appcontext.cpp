#include "appcontext.h"

#include "flowviewapp.h" // app->graph() / app->nodeFactory()

#include <lain/flow/edit.h> // edit::addNode
#include <lain/flow/graph.h>
#include <lain/gui/dock.h>	// activateWindowTab (bring the Graph tab forward)
#include <lain/gui/nodes.h> // imnodes selection + grid-space placement
#include <lain/math/types.h>

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

	flow::NodeId AppContext::addCatalogNode(const std::string& key)
	{
		flow::Graph& graph = app->graph();
		const flow::NodeId id = flow::edit::addNode(graph, app->nodeFactory().create(key));
		const float offset = 40.0f + static_cast<float>(addCounter % 6) * 28.0f;
		gui::nodes::SetNodeGridSpacePos(static_cast<int>(id.value()), math::Vec2f{offset, offset});
		++addCounter;
		return id;
	}
} // namespace flowview
