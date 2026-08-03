#pragma once

#include "lain/flow/boundary.h" // BoundaryInput / BoundaryOutput — the enumerated pin handles
#include "lain/flow/node.h"
#include "lain/flow/types.h"

#include <cstddef>
#include <map>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace lain::flow
{
	// The identities of a graph's invariant boundary pair. Handed to the Graph constructor by a
	// LOADER, so the pair is *born* with the ids the document recorded rather than being re-keyed
	// afterwards — a NodeId is immutable once its node is admitted (ADR-0011). A null id (or a
	// second use of the same one) is minted instead, so construction is total: a hand-written or
	// truncated document that names neither boundary node must still open.
	struct BoundaryIds
	{
		NodeId input;
		NodeId output;
	};

	// Owns the nodes and the edges between their ports: a pure data model. Builds
	// the DAG (add / connect / disconnect) and exposes a dependency-respecting
	// order. It does not execute — a Scheduler (see scheduler.h) consumes a Graph
	// and evaluates it (push run, or pull of one node's upstream).
	class Graph
	{
	public:
		// Every Graph is born with its INTERFACE: exactly one GroupInputNode and one
		// GroupOutputNode, both pinless. A graph with no boundary pair would have no path in or
		// out, and several would be redundant (one boundary node already grows dynamic pins) — so
		// the pair is an invariant of the data model, like acyclicity, not editor policy. It is
		// created here, removeNode refuses to drop either, and add refuses a second. This mints the
		// pair's ids; the BoundaryIds overload restores saved ones.
		Graph();
		explicit Graph(BoundaryIds boundary);

		// Construct a node of type T (must derive from Node) in place; returns its id (or the null
		// NodeId if the add was refused — see the type-erased overload).
		template <typename T, typename... Args>
		NodeId add(Args&&... args);

		// Adopt an already-constructed node (e.g. produced by a factory), MINTING its identity;
		// returns its id. The type-erased entry point the template add<T> forwards to. REFUSES
		// (returning the null NodeId) a second GroupInputNode / GroupOutputNode — the graph already
		// has its pair, and a loader rebuilding a saved graph adopts that pair rather than adding
		// to it.
		NodeId add(std::unique_ptr<Node> node);

		// Adopt a node with a REQUESTED identity — how a loader restores a saved node *as itself*.
		// Returns the id actually given, which differs from `requestedId` when that id was null or
		// already taken: identity is never overwritten, so a duplicate is re-minted and the caller
		// (which compares the two) reports it. Refuses a second boundary node exactly as above.
		NodeId add(std::unique_ptr<Node> node, NodeId requestedId);

		// Remove a node and every edge touching it (in or out); returns whether it
		// existed. Other nodes keep their ids — ids are handles, not positions. REFUSES (returning
		// false) either boundary node: losing one would strip the graph of its interface and, for a
		// group's inner graph, silently drop every edge the parent had wired to it.
		bool removeNode(NodeId id);

		std::size_t nodeCount() const { return m_nodes.size(); }
		bool contains(NodeId id) const { return m_nodes.count(id) != 0; }

		// Every node id, in INSERTION order — a stable enumeration of all nodes (unlike topoOrder,
		// which is dependency order). Kept explicitly, because a uuid's ordering is not a graph
		// order (see NodeId): the id-keyed map is for lookup, this vector is for order. The order a
		// serializer walks nodes in, and the order the JSON `nodes` array restores. Invalidated by
		// add / removeNode, like any container reference.
		const std::vector<NodeId>& nodeIds() const { return m_order; }
		Node& node(NodeId id) { return *m_nodes.at(id); }
		const Node& node(NodeId id) const { return *m_nodes.at(id); }

		// Connect an output to an input. Type-checked (declared types must match),
		// single-source (an input takes one edge), and cycle-rejecting — so the
		// graph stays acyclic by construction. The edge stores the ports' stable
		// PortAddresses, so it survives the ports being reordered/renumbered.
		Connection connect(PortAddress from, PortAddress to);

		// Index convenience: connect the from-node's `outPort`-th output to the to-node's
		// `inPort`-th input. Resolves the indices to PortAddresses *now* (the edge stores the
		// stable ids, never the indices), so it is safe for building fixed graphs / tests.
		Connection connect(NodeId from, std::size_t outPort, NodeId to, std::size_t inPort);

		// Remove the edge feeding `input`, if any; returns whether one was removed.
		bool disconnect(PortAddress input);
		bool disconnect(NodeId to, std::size_t inPort); // index convenience (resolves now)

		// Record that `id`'s recipe changed, so every Evaluation recomputes it on its next run. The
		// primitives here call it themselves; it is public for the editing layer, which composes
		// gestures that change a node's shape in ways no single primitive covers (edit::syncGroupPorts
		// re-deriving a group's ports from its interior). Not a general "invalidate" hook — a caller
		// that changes nothing must not call it, since a version bump costs every evaluation a re-run.
		void bumpNodeVersion(NodeId id) { bump(id); }

		// Remove a dynamic port. A *primitive*: it REFUSES (returns false) if any edge still
		// touches the port, keeping the no-dangling-edge invariant total — the safe gesture
		// edit::removePort disconnects the incident edges first. Returns whether the port was
		// removed.
		bool removePort(PortAddress port);

		// A directed edge: an output PortAddress feeding an input PortAddress. Addressed by
		// stable PortId, never std::size_t, so it survives dynamic-port mutation.
		struct Edge
		{
			PortAddress from;
			PortAddress to;
		};
		const std::vector<Edge>& edges() const { return m_edges; }

		// Nodes in a dependency-respecting order (sources first). Acyclic by construction, so this
		// always covers every node.
		//
		// Maintained by the mutators, NOT computed lazily on first query. That is a threading claim,
		// not tidiness: a `const Graph&` must be CONCURRENTLY READABLE (ADR-0012), because N
		// evaluations may run over one definition at once — and a lazy cache into mutable members is
		// exactly what two of them would rebuild simultaneously after an edit, while the `const&`
		// signature advertised safety. Editing is human-paced and runs are hot, so the cost lands in
		// the right place.
		const std::vector<NodeId>& topoOrder() const { return m_topo; }

		// The graph's I/O boundary — a flat list of pins across every boundary node (see boundary.h),
		// as immutable RECIPE metadata: where each pin is, its name, its declared type. A host loops
		// these, binds each input through its Evaluation, runs, then reads each output from the same
		// Evaluation. Const, because describing an interface changes nothing — and because the values
		// these used to reach are not on the definition any more. Node order then pin order, so the
		// list is stable.
		std::vector<BoundaryInput> boundaryInputs() const;
		std::vector<BoundaryOutput> boundaryOutputs() const;

		// The graph's boundary input / output node — for a host that EDITS the interface (the ±
		// add/remove pins), where the flat pin lists above are for a host that just binds. Returned
		// by reference, never null: the pair is a construction invariant (see the constructor), so
		// there is always exactly one of each and no caller needs a null check.
		//
		// The const overloads matter beyond tidiness: READING a graph's interface (serializing a
		// linked group's pins, listing a boundary) is a const operation, and without them every such
		// reader had to const_cast its way in — which under ADR-0012, where `const Graph&` comes to
		// mean *concurrently readable*, is exactly the wrong habit to leave lying around.
		GroupInputNode& boundaryInputNode();
		GroupOutputNode& boundaryOutputNode();
		const GroupInputNode& boundaryInputNode() const;
		const GroupOutputNode& boundaryOutputNode() const;

	private:
		bool valid(NodeId id) const { return m_nodes.count(id) != 0; }
		// Is `target` reachable from `start` by following edges (start included)?
		bool reaches(NodeId start, NodeId target) const;
		// Is `id` one of the two boundary nodes? (Backs the removeNode refusal.)
		bool isBoundary(NodeId id) const { return id == m_boundaryIn || id == m_boundaryOut; }
		// `requested` if it is usable (non-null and not already taken), else a freshly minted id.
		// The one place identity is decided, so "null means mint" and "a duplicate is re-minted"
		// are the same rule for the constructor and for both add() overloads.
		NodeId usableId(NodeId requested) const;
		// Take ownership unconditionally at `id` — what add() does once it has approved the node,
		// and the only way the constructor can seat the pair that add() would refuse.
		NodeId adopt(std::unique_ptr<Node> node, NodeId id);
		// Recompute the dependency order. Called by every mutator that changes nodes or edges.
		void rebuildTopoOrder();
		// Record that `id`'s recipe changed, so evaluations recompute it (Node::version).
		void bump(NodeId id);

		// Nodes keyed by id, not stored by position — an id outlives the removal of
		// other nodes. Ordered so lookup is logarithmic; iteration order is a uuid order, which
		// means nothing, so anything order-sensitive reads m_order below.
		std::map<NodeId, std::unique_ptr<Node>> m_nodes;
		// Insertion order, maintained alongside the keyed owner: identity lookup and node order are
		// separate concerns, and a uuid supplies only the first (ADR-0011).
		std::vector<NodeId> m_order;
		// The interface pair, seated by the constructor. Held as ids so the accessors are a lookup
		// rather than an RTTI scan. (A moved-from Graph keeps these ids with no nodes behind them —
		// as for any moved-from object, only destruction and assignment are valid on it.)
		NodeId m_boundaryIn;
		NodeId m_boundaryOut;
		std::vector<Edge> m_edges;
		// Rebuilt by rebuildTopoOrder() whenever nodes or edges change — never lazily, so reading it
		// through a const Graph& is safe from several threads at once.
		std::vector<NodeId> m_topo;
	};
} // namespace lain::flow

#include "lain/flow/details/graph.inl"
