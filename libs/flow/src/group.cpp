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
} // namespace lain::flow
