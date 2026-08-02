#include "appcontext.h"

#include "flowviewapp.h" // app->graph() / app->nodeFactory()
#include "groupnav.h"	 // the active graph an add lands in

#include <lain/flow/edit.h> // edit::addNode
#include <lain/flow/graph.h>
#include <lain/flow/node.h> // setName (unique default titles)
#include <lain/gui/dock.h>	// activateWindowTab (bring the Graph tab forward)
#include <lain/gui/gui.h>	// GetContentRegionAvail (the thumbnail box)	// activateWindowTab (bring the Graph tab forward)
#include <lain/gui/nodes.h> // imnodes selection + grid-space placement
#include <lain/math/types.h>

#include <algorithm>
#include <string>

namespace flowview
{
	using namespace lain;

	math::Vec2f previewFit(math::Vec2i extent, math::Vec2f box)
	{
		const float w = static_cast<float>(extent.x);
		const float h = static_cast<float>(extent.y);
		if (w <= 0.0f || h <= 0.0f || box.x <= 0.0f || box.y <= 0.0f)
			return math::Vec2f{0.0f, 0.0f};
		// The smaller ratio is the one that fits BOTH ways: width-limited for a wide image, and
		// height-limited for a tall one. Drawing at {side, side} (as this used to) squashed every
		// non-square image into a square.
		const float scale = std::min(box.x / w, box.y / h);
		return math::Vec2f{w * scale, h * scale};
	}

	math::Vec2f thumbnailBox()
	{
		return math::Vec2f{gui::GetContentRegionAvail().x, kThumbnailMaxHeight};
	}

	void AppContext::locateNode(flow::NodeId id)
	{
		gui::nodes::ClearNodeSelection();
		gui::nodes::SelectNode(canvas.node(id));
		locateTarget = id;				 // centred on the next Graph draw (needs the node's drawn size)
		gui::activateWindowTab("Graph"); // bring the canvas forward so the located node is visible
	}

	// Give a freshly added node a name no other node in the graph is using: "tint", then "tint 2",
	// "tint 3", … Node names are display only (flow addresses nodes by NodeId and imposes no
	// uniqueness), so this is purely the viewer's readability policy — the canvas title shows the name
	// alone, and two identical titles would be indistinguishable at a glance. A user rename can still
	// collide; that is their call, not something to police.
	static void uniquifyName(const flow::Graph& graph, flow::Node& node)
	{
		const std::string base = node.name();
		const auto taken = [&](const std::string& candidate)
		{
			for (const flow::NodeId other : graph.nodeIds())
			{
				if (other != node.id() && graph.node(other).name() == candidate)
					return true;
			}
			return false;
		};

		if (!taken(base))
			return;
		for (int suffix = 2;; ++suffix)
		{
			const std::string candidate = base + " " + std::to_string(suffix);
			if (!taken(candidate))
			{
				node.setName(candidate);
				return;
			}
		}
	}

	flow::NodeId AppContext::addCatalogNode(const std::string& key)
	{
		// Adds land in the ACTIVE graph — the level the user is looking at — not the root. And nothing
		// is added inside a linked group: its recipe belongs to its template (Group > Edit Template...).
		if (!editableAt(app->graph(), activePath))
			return flow::NodeId{};
		flow::Graph& graph = resolvePath(app->graph(), activePath);
		const flow::NodeId id = flow::edit::addNode(graph, app->nodeFactory().create(key));
		if (id == flow::NodeId{})
			return id;
		uniquifyName(graph, graph.node(id));
		const float offset = 40.0f + static_cast<float>(addCounter % 6) * 28.0f;
		gui::nodes::SetNodeGridSpacePos(canvas.node(id), math::Vec2f{offset, offset});
		++addCounter;
		return id;
	}
} // namespace flowview
