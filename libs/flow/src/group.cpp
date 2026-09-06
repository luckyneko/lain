#include "lain/flow/group.h"

#include "lain/flow/porttyperegistry.h" // listTypeFor — the only way to lift a runtime type

namespace lain::flow
{
	// A map mirrors its interior's pins LIFTED: an inner pin of type T becomes an outer port of type
	// vector<T>, so the node's face takes and delivers whole collections while its interior is
	// written against one element (ADR-0014).
	//
	// The lift has to go through the port-type registry, and that is not an implementation shortcut:
	// a PortType can say what its ELEMENT type is, but nothing can walk that backwards, because
	// naming std::vector<T> requires T at compile time and reconciliation has only a runtime type.
	// So a type is mappable exactly when its list form has been registered — which an app must do
	// anyway for a collection pin to survive a save, since a dynamic pin is replayed through its key.
	PortId MapNode::exposePort(Port::Direction outerSide, const Port& innerPin, Presence presence)
	{
		const PortType* lifted = listTypeFor(innerPin.type());
		if (lifted == nullptr)
			return PortId{}; // no collection form for this type — add nothing rather than degrade

		const PortId outer = (outerSide == Port::Direction::Input)
								 ? addInputLike(innerPin.name(), *lifted, presence)
								 : addOutputLike(innerPin.name(), *lifted);
		mapPort(outer, innerPin.id());
		return outer;
	}

	// A loop is born with its bound and its report, and its interior with the two pins the ENGINE
	// talks to it through. The reserved pins are declared FIRST on each boundary node, so they are
	// deterministically PortId{1} there and a loaded document's replayed pins land after them.
	LoopNode::LoopNode()
		: GroupNode("Loop")
	{
		// A DEFAULTED input rather than a param: every iteration evaluates one definition, so a
		// bound that is meant to be graph-driven cannot be per-node configuration (the 2026-08-15
		// param audit's rule, and the same reason a map's per-element settings had to become pins).
		// Default 1 — a fresh loop behaves exactly like a group, which is the least surprising thing
		// on a canvas; 0 is the fold identity and a legitimate value, just not a sensible default.
		m_count = addInput<int>("count", Default{1});
		m_iterations = addOutput<int>("iterations");

		establishReserved(m_inner);
	}

	void LoopNode::establishReserved(Graph& interior, const std::string& indexName, const std::string& continueName)
	{
		m_index = interior.boundaryInputNode().addReserved<int>(indexName);
		// True, so an unwired condition is TRANSPARENT (the count decides) while a wired-and-
		// suppressed one stays empty and means the iteration failed — GateNode::enable's 2026-08-15
		// resolution, reused rather than reinvented for the same trap.
		m_continue = interior.boundaryOutputNode().addReserved<bool>(continueName, Default{true});
	}

	bool LoopNode::pairCarry(PortId innerIn, PortId innerOut)
	{
		const GroupInputNode& into = m_inner.boundaryInputNode();
		const GroupOutputNode& from = m_inner.boundaryOutputNode();

		const Port* in = into.findOutput(innerIn);
		const Port* out = from.findInput(innerOut);
		if (in == nullptr || out == nullptr)
			return false; // one half names no pin on the boundary node it should be on

		// The engine writes `index` and reads `continue`; carrying one would be a second driver.
		if (innerIn == m_index || innerOut == m_continue)
			return false;

		// addCarry<T> makes both pins the same type by construction; a document can name a pair that
		// is not, and nothing downstream would notice — Evaluation::bind type-checks nothing.
		if (in->type() != out->type())
			return false;

		for (const auto& [pairedIn, pairedOut] : m_carries)
		{
			if (pairedIn == innerIn || pairedOut == innerOut)
				return false; // already half of another carry
		}

		m_carries[innerIn] = innerOut;
		return true;
	}

	bool LoopNode::mirrorsPin(Port::Direction outerSide, const Port& innerPin) const
	{
		// By id, never by name: renaming `index` must not quietly turn a while loop into a count
		// loop. The two reserved pins live on different nodes and are therefore BOTH PortId{1}, so
		// the direction is what picks which one to compare against — portMap()'s standing asymmetry.
		const PortId reserved = (outerSide == Port::Direction::Input) ? m_index : m_continue;
		return innerPin.id() != reserved;
	}

	PortId LoopNode::exposePort(Port::Direction outerSide, const Port& innerPin, Presence presence)
	{
		// A loop is the first group kind with ports of its OWN, so it is the first where an inner
		// pin's name can collide with one (`count`, `iterations`). addInputLike / addOutputLike
		// assert on a duplicate — a duplicate would also make a name-addressed edge ambiguous on
		// disk — so it is refused here, adding nothing rather than degrading.
		if (hasPortNamed(outerSide, innerPin.name()))
			return PortId{};

		return GroupNode::exposePort(outerSide, innerPin, presence);
	}

	void LoopNode::reconcileInterior()
	{
		const GroupInputNode& into = m_inner.boundaryInputNode();
		const GroupOutputNode& from = m_inner.boundaryOutputNode();
		for (auto it = m_carries.begin(); it != m_carries.end();)
		{
			if (into.findOutput(it->first) != nullptr && from.findInput(it->second) != nullptr)
				++it;
			else
				it = m_carries.erase(it); // half of it is gone — the pairing is no longer a fact
		}
	}
} // namespace lain::flow
