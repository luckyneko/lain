# A node's payload type is data, and a conversion is a node

---
Status: accepted
Extends [ADR-0005](0005-node-parameters-distinct-typed-slots.md) (a payload type is *not* a param) and
follows [ADR-0014](0014-map-nodes-staged-planning.md)'s rule for where per-type behaviour lives
---

`LoopNode`'s `index` is an `int`; `CompareNode`'s inputs were `float`; `Graph::connect` type-checks
exactly. So the loop built in M11 could not be driven from the GUI at all — there is **no conversion
anywhere in `flow`**, implicit or explicit, and no way to author one.

That is an instance of a wider gap. A node's payload type was baked into its **factory key**:
`ConstantNode<T>` cost one palette entry per type (five, against a registered port-type set of ten and
growing), and `GateNode` / `MergeNode` / `SelectNode` existed for `image::Image` alone — so a graph
could not gate a float, merge bools or select between paths. And the node that would fix the original
problem, a **Cast**, is impossible in that model at any size: it needs one key per *pair*.

## Decision

**A node may declare named PAYLOAD TYPES — types its ports and params are declared from, chosen by
whoever authored the graph and stored with the document. A conversion between two payload types is a
registry the app fills, read by a Cast node and by nothing else.**

- **Named, because one is not always enough.** A Constant has one (`value`); a Compare has one; a Cast
  has two (`from`, `to`). The name says what the type is *for* on that node.
- **A payload type is a `const PortType*`, not a registry key.** The string is only the on-disk form.
  So a typed node is still constructible with no registry populated — `ConstantNode{portType<int>()}`
  — which `libs/flow`'s own tests need, and only *saving* one requires a registration.
- **It is emphatically NOT a param.** A `Node` has no access to its `Graph`, so it cannot disconnect
  its own edges; a retype can therefore never be a node-local write, and never `Node::setParam`.
- **Retyping is IN PLACE.** A declaration's identity is its `PortId` and its type is a field, so a
  retype assigns the field. Ids, names, order and presence survive.
- **One writer: `Graph::setPayloadType`, the primitive, reached through `edit::setPayloadType`, the
  gesture.** The primitive REFUSES while any edge incident on a declaration it would move would be
  left with ends that disagree; the gesture cuts exactly those edges first and **reports** them, and
  carries each affected param's value across through the conversion registry, reporting what it could
  not carry. Precisely the split `Graph::removePort` / `edit::removePort` already has.
- **Per-type behaviour follows ADR-0014's rule.** Derivable from `T` at declaration → a `PortType`
  field; needs an app-supplied registration → a registry. So **ordering** (`PortType::compare`, filled
  under `if constexpr` on `meta::is_less_comparable_v<T>`) is a field, and **conversion** is a
  registry keyed by the type pair.
- **`connect` never consults the conversion registry.** An edge type-checks exactly. A conversion is
  something a graph asks for by *containing a node that does it*.
- **`float → int` truncates toward zero**, and `bool ↔ int` is deliberately not registered.
- **An unconvertible PAIR is representable, and reported rather than refused.** A Cast may hold two
  payload types the registry cannot bridge; it produces nothing, and the host says so.
- **No schema version bump.** The document gains one optional `types` section per node; **absent means
  the factory's preset stands**, exactly as an absent `params` means the declared defaults do.

## Why

**A Cast is the forcing function; Constant is the beneficiary.** Collapsing five Constant keys into
one is a refactor with no new capability, and this repo's record is that a mechanism built without a
caller stays unreachable — four separate findings. The mechanism exists because a Cast cannot be built
any other way, and Constant, Compare and the control nodes ride on what a Cast forced into existence.

**The type cannot be a param, and the reason is structural rather than stylistic.** `setParam`
type-checks, commits and invalidates as one operation — and that is all it can do, because a `Node`
cannot reach its `Graph`. Meanwhile the Inspector renders **every** param through the type-keyed
`ParamEditors` and commits every one through `setParam`, so a type stored as a param would be reached
by that generic loop automatically. The result would be an edge whose ends disagree — and **nothing
re-checks an edge after `connect`**: `Scheduler::populateInputs` copies the upstream value into the
slot blind, so the failure surfaces as a `bad_any_cast` thrown inside a worker task, far from the
click that caused it. Naming it a "type param" would also collide head-on with ADR-0005's definition
of a Param as a value `compute()` reads.

**Retyping in place rather than remove-and-re-add.** A re-added port draws a fresh `PortId`, which
would dangle every edge, canvas id and layout entry that named the old one. In place, a retype is
exactly what it claims to be: the same declaration, of a different type.

**The refusal is spelled as "would these ends still agree?" rather than "is this port being
retyped?".** The former IS the invariant. The two coincide today — `connect` is the only thing that
makes an edge and it type-checks, so an existing edge's other end always carries the port's current
type, and a genuine retype therefore breaks *every* edge on *every* port it moves. Edges on the node's
untagged ports (a Gate's bool `enable`) are never examined and survive. Stating the invariant rather
than its present consequence is what keeps the primitive correct if a second edge-maker ever appears.

**Ordering is a `PortType` field because it is derivable; conversion is a registry because it is
not.** This is exactly the split the collection capability already makes: `PortType::element`
(vector → element) is a field because a `PortType` for `vector<T>` can compute it, while `listTypeFor`
(element → vector) is a registry because nothing can name `std::vector<T>` from a runtime
`type_index`. A conversion is a relation *between* two types, and nothing can enumerate the second at
the point a `PortType` for the first is built.

The payoff is that a Compare's accepted types are **derived, not listed**: `acceptsPayloadType` asks
`PortType::isOrderable()`, so the set grows with the registered types instead of rotting — the same
"no separate flag, no registry to keep in step" argument `PortType::element` makes for mappability.

**No implicit coercion, no wildcard pin.** Four alternatives were considered and refused:

- **Coercion inside `connect`** — the type check stops being a check, and lossy conversion becomes
  invisible. Directly against ADR-0020's *no silent lossy conversion*, already in force for colour.
- **Pins that adopt the type of whatever connects** (Blender-style wildcards) — `connect` would mutate
  the node, disconnection has no defined answer, and a saved graph's types would be derived from its
  edges, so a broken edge silently retypes it. A fact in two places that can disagree — the shape M7
  slice 1, ADR-0014 and M11 slice 1 each deleted.
- **An `Any`-typed input on Cast**, to save its second dropdown — this trades a *visible* failure for
  an *invisible* one. With two declared types, setting `from` wrongly means the edge refuses to
  connect, at the moment of the mistake. With `Any`, the graph connects happily and produces an empty
  output at run time.
- **Collapsing `int` and `float` into one numeric type** — dissolves the original blocker, but
  `count` / `index` / `FramePosition` are genuinely integral, and it does nothing for the path, string
  and image families.

**`float → int` truncates, and that is stated rather than inherited.** A Cast is the user explicitly
asking for a lossy conversion, so lossiness is not the problem; *undocumented* lossiness is. Truncation
is C++'s own conversion and what a node named Cast should mean, and `2.9 → 2` is visible immediately.
Rounding is a different operation and belongs to a numeric node that says so — not to a param on Cast
whose meaning would exist for only some type pairs.

**An unconvertible pair must be representable, and that was forced by the loader.** Refusing one
through `acceptsPayloadType` — so an invalid Cast could not exist — is the shape this repo normally
prefers, and it is wrong here: **the loader applies payload types one at a time**, so a document
saying `Path → String` would have its `from` refused while `to` was still the factory's `Float`
preset, and the node would come back as something other than what was saved. Making the pair valid at
every intermediate step would need either a retry pass or a whole-node apply, both of which are
machinery to defend a state that costs nothing to allow. So the pair is checked where it can be
answered whole — `CastNode::canConvert()` — and the host reports it in the Issues pane, which is
where every other "configured into a state that cannot work" already surfaces.

That report is the one place the adapter asks a **class** rather than a structural fact (the Issues
pane's map-hole row asks `interiorEvaluation()`). There is no structural fact to ask: nothing in
general says two of a node's payload types must be bridged by a conversion, and inventing a `Node`
virtual for a single caller would be a seam with nothing behind it. flowview already names every node
kind — its factory, its palette, its canvas colours — so a `dynamic_cast` there is not the layering
break the same line would be in the scheduler.

**`bool ↔ int` is absent because nothing has decided what it means.** What `2 → true` should do is a
policy no caller is asking for, and an absent conversion is a menu entry that never appears rather
than a wrong answer nobody notices.

**A retype carries a param's value across through the same registry a Cast uses.** So the registry
means one thing everywhere, rather than the retype having a private idea of what an `int` becomes.
What cannot be carried is reset to the new type's default **and reported by name** — a number silently
becoming 0 is exactly the failure that reporting exists to prevent.

**No version bump, and the five old keys stay registered.** `kind` is a factory key, not schema, and
the document gains one optional section — additive, which v2 already tolerates. `constInt` and its
four siblings remain as `Factory` **string-creator** registrations, which record no reverse
type → key entry. That detail is load-bearing: `Factory::keyOf` maps one class to exactly one key, so
registering them the typed way would make it answer whichever registered last, and a Float constant
would **save as `constInt`** carrying a `types` section contradicting its own kind — a document that
still loads, and lies.

## Consequences

- **`ConstantNode<T>` is no longer a template**, and `constantOf<T>(value)` is the direct replacement
  for building one with a compile-time type. `GateNode` / `MergeNode` / `SelectNode` followed, and
  `CompareNode` with them.
- **A DYNAMIC pin has to be tagged as it arrives** (`Node::tagAs`, called from
  `onDynamicPortAdded`). A Merge's branches are added at runtime through the port-type registry,
  whose creator has a compile-time `T` and knows nothing about payload types — so without the tag a
  retype would move the constructor-declared `out` and leave every branch behind, and the node would
  forward nothing while looking correctly configured. This is why Merge and Select were in scope:
  they are the case that stops the mechanism being quietly fitted to fixed-pin nodes.
- **A control node's non-payload pins are untouched by a retype** — a Gate is switched by a bool and
  a Select indexed by an int whatever they carry, so neither is declared from the payload type.
- **Merge and Select now TYPE-CHECK the branch they forward.** `acceptsPortType` is the *host's*
  filter and `addDynamicPort` answers to nobody, so a mistyped branch must produce nothing rather
  than put a wrongly-typed value in a slot that declares another type.
- **`Node` grows the payload-type storage and one virtual** (`acceptsPayloadType`). It is a plain
  accessor rather than the nullable virtual `innerGraph()` / `iterationPorts()` use, because the
  tagging must live on `Node` anyway — a virtual answering a fact `Node` already holds would be a
  second source able to disagree with the first.
- **`meta::is_less_comparable` asks a container about its ELEMENT.** Before C++20 `std::vector`'s
  `operator<` is declared for every element type and fails only when *instantiated*, so plain
  detection answers true for `std::vector<T>` whatever `T` is, and the first real comparison is a hard
  error inside `<algorithm>` rather than a false from the trait. Found by the build, not by reading.
- **Every per-type bridge must be TOTAL in the value it is handed.** A retype makes a port's declared
  type disagree with the value the last run left in the evaluation — a state that could not exist
  before, since a port's type was fixed at declaration and a slot held that type or nothing. And the
  retype *cannot* clear it: invalidation is pulled (ADR-0012), so a definition holds no list of its
  evaluations. It bumps the node's version and the next run corrects the slot, but a host draws in
  between. So `PortType::describe` and the four collection bridges test `holds<T>()`, not
  `empty()`, and `describe` answers **"(stale)"** — deliberately not `"(empty)"`, which means
  SUPPRESSED (ADR-0007) and would be a lie about a port that has a value, just not one of its type
  yet. The same rule applies to any adapter reading a value: ask what the VALUE holds, never what the
  port declares.
- **A retype is not undoable by itself** — it goes through the host's ordinary edit path, so it is
  captured by the next snapshot like any other change.
- **The canvas offering to insert a Cast on a mismatched drag is deliberately out.** The registry makes
  it possible, but it is an adapter gesture with its own design (what if two conversions exist? none?),
  and it is recorded in WORK.md rather than guessed at here.

## Deliberately unsettled

- **Whether a payload type may be a collection** (a Constant emitting a `vector<Image>`). Nothing
  refuses it and nothing tests it; the first caller decides what it should mean.
- **Whether `acceptsPayloadType` should be able to refuse per-VALUE** rather than per-type. No caller.
- **Rounding, saturating and checked numeric conversions.** One numeric node, when something asks.
