# A loop node runs one definition until it says stop, carrying state between iterations

---
Status: accepted
Settles the last question [ADR-0012](0012-definition-and-evaluation.md) left deliberately unsettled,
which [ADR-0014](0014-map-nodes-staged-planning.md) re-deferred
---

`flow` can run a subgraph **once** (a group) and **N times independently** (a map). It cannot run one
**sequentially, feeding each pass into the next**. ADR-0012 named *"whether Loop carries state between
iterations"* among four questions it refused to guess at; ADR-0014 settled the other three for the map
and re-deferred this one, because *"a map's children are independent by construction, a loop's are
not, which is a different execution shape rather than a variant of this one"*.

**There is no production caller, and this ADR does not pretend otherwise.** The owner's framing was
*"there is no direct use case as much as this is a feature that really tests the node approach."* That
is a reason to hold the design to a **stricter** standard than M8's, not a looser one — ADR-0012's own
warning is that *"guessing produces a mechanism fitted to imagined requirements"*. So every decision
below is either forced by a rule already in force, or is the smallest thing that makes the two shapes
the owner asked for — a count loop and a while loop — actually work. Everything else is refused and
listed at the end.

## Decision

**A loop is a group whose interior is evaluated once per ITERATION, where each iteration's carried
outputs seed the next one's inputs, and whose trip count is bounded by construction.**

- **One `LoopNode`: bounded, plus an optional condition.** A node-owned `count` input carrying a
  `Default{1}` (*amended at the build, 2026-09-05: the value is **1**, so a fresh loop behaves exactly
  like a group — `0` is the fold identity and a perfectly good value, just not a sensible thing for a
  node to do the moment it lands on a canvas; `int` on both sides, because that is the type a graph
  can actually drive*) and an **optional** inner `continue : bool`. A count loop is the count alone; a while
  loop is the condition with the count as its safety bound; a converging solver is both, which is what
  real solver code already does. A loop therefore **cannot fail to terminate**, and that is not only
  user safety — see *Why*.
- **A carry is a stored `PortId` pair, and only a paired gesture can create one.** `LoopNode` holds
  `map<PortId innerIn, PortId innerOut>`, mirroring `GroupNode::m_outerToInner` and id-keyed for the
  reason that header already gives: *"renaming an inner pin must keep the outer wiring, which is the
  whole reason ports carry stable ids"*. `addCarry<T>(name)` adds the pin to **both** inner boundary
  nodes and records the pair atomically, so half a carry cannot be authored.
- **Two reserved inner pins, never mirrored.** The interior is born with `index : int` on the inner
  `GroupInputNode` and `continue : bool` on the inner `GroupOutputNode` — the same shape as a `Graph`
  being born with its boundary pair. `LoopNode` stores their `PortId`s and `edit::syncGroupPorts`'s
  *add* phase skips them **by id, never by name** — through `GroupNode::mirrorsPin`, *not* through
  `exposePort` (*amended at the build: those are different questions. A reserved pin is **not a
  candidate** for mirroring and is silent; a pin `exposePort` **refuses** is a candidate a user can act
  on and a host should name. The first cut conflated them, and `GroupSync::refused` then named `index`
  and `continue` on every pass for the life of the document.*). They are declared through a new
  `GroupInputNode` / `GroupOutputNode` **`addReserved`** seam, as **static** pins (*amended at the
  build: `addBoundary` routes through `addDynamicPort`, which marks a pin dynamic — so serialization
  would replay `index` onto a constructor that already made it — and has no `Default` overload, while
  `Node::addInput(name, Default<T>)` is protected. Static is `SelectNode::selector`'s precedent
  exactly: replay skips it, the ctor rebuilds it on load, an edge to it resolves by name like any
  other, and `continue`'s default round-trips as an ordinary `Param`.* — **corrected at slice 5,
  2026-09-06: "the ctor rebuilds it on load" is exactly where the `selector` analogy breaks.** A
  Select's static pin is on the node the factory made; a loop's are on its INTERIOR, and a load
  replaces that interior wholesale, moving the constructor's pins away with it. The LOADER must
  declare them onto the graph it is filling, before that body's edges resolve, and under the name
  the document recorded — a reserved pin is renameable, and an edge into one is name-addressed like
  any other.*). `continue` carries
  `Default{true}`, so **unwired means the count decides** while **wired-and-suppressed stays empty and
  means the iteration failed**. (*Amended at slice 6, 2026-09-06: static is also what makes a reserved
  pin **not the user's to remove**. An editor's per-pin × goes through `edit::removePort`, and
  `Graph::removePort` erases a static port as happily as a dynamic one — so an Interface panel removes
  only `isDynamic()` pins. Asked of the PIN rather than of the level, so the panel needs no idea which
  node kinds have reserved pins. Renaming stays available.*)
- **A loop folds; it never scans.** Each carry mirrors out as its **final** value, each unpaired inner
  output as its **last-iteration** value, plus one node-owned `iterations : int`. A map maps, a loop
  folds. (*Amended at the build: deriving seed / invariant / final / last needs **no** special
  mirroring — it is identical to a plain group's, because the pairing changes what the engine does
  BETWEEN iterations and never what the ports look like. `LoopNode::exposePort` overrides only to
  refuse.*)
- **One iteration per stage.** The loop raises a frontier; between stages the coordinator reads
  iteration *k*'s carry outputs and `continue`, seeds *k+1*, and re-plans. In `expand`, an iterating
  loop emits its interior steps **and re-raises itself as a frontier**; a finished loop emits
  `LoopExit` alone. Stages = 1 + N + 1.
- **Exactly one retained child evaluation, reused each iteration.** A map retains N because its
  elements are co-equal *results*; a loop's iterations are *steps* toward one. `EvalPath`'s index
  stays 0, as for a group.
- **Suppression is failure; `continue` is break.** An empty carry output or an empty `continue` clears
  the loop's whole output, and ADR-0007 carries it downstream unchanged. **`count == 0` is not that
  case**: zero iterations run and each carry delivers its **seed** — the fold identity over an empty
  sequence, exactly as a map's `N == 0` yields an empty vector.
- **The pairing is stored; the ports are derived.** A `loop` section holds `carries: [{in, out}]` plus
  the `index` / `continue` pin names, all **name-addressed like every edge in this format**. The outer
  ports are re-derived by `edit::syncGroupPorts`, so a loop follows M5's group rule rather than the
  map's. (*Built 2026-09-06. The pin names proved REQUIRED rather than a convenience —
  sabotage-verified: dropping them turns a converging while loop into one that runs to its bound,
  110 instead of 13. The section is read whole and applied in two parts, because the names must reach
  the interior before its edges resolve while the carries need those pins to already exist.*)
- **Inline only**, and a linked loop is *refused* rather than deferred, for the same reason a linked
  map is (ADR-0014, amended 2026-09-09): a loop whose interior holds a **linked group** is what one
  would have been for, and it keeps the template owning the recipe while the loop's own boundary
  keeps owning the carry pairing — one owner per fact, and room inside the body for work the template
  does not do.

## Why

**Bounding the loop is what preserves the scheduler's termination proof.** `runStages` terminates
today because *"a map is deferred at most once per invocation, so the number of stages is bounded by
the number of maps"*. A loop is deferred once per iteration, which destroys that argument outright. A
mandatory `count` restores it — the bound on stages becomes the bound on iterations. So the guard is
not a usability nicety bolted onto an unbounded mechanism; it is the mechanism's own well-formedness
condition, and an unbounded loop would have made the staging loop's termination undecidable from
inside the scheduler. Making `count` a defaulted input rather than an optional one means there is no
"unbounded" state to represent, so nothing has to refuse it at runtime.

**The union is the shape real loops have.** A pure while loop would leave a runaway able to hang
flowview, and a pure count loop cannot express convergence at all. Every production convergence
routine in existence carries a max-iteration bound, so "condition **and** bound" is not a compromise
between the two requested shapes — it is the thing both of them are special cases of. Two node kinds
were rejected because the carry, the mirroring, the derived interface, the serialization, the staging
and the breadcrumb are identical for both, so it would duplicate every hard part for one predicate;
a mode enum was rejected because a stored flag that can disagree with the pins it describes is exactly
what M7 slice 1 and ADR-0014 each deleted.

**Names cannot carry the pairing, because a rename must not change behaviour.** Pairing inner pins by
name needs no stored state and matches the on-disk edge schema, which makes it genuinely tempting. It
fails on the failure mode: renaming one half silently unpairs the carry, and the loop keeps running
and quietly stops carrying — no error, no empty value, just a different answer. That is strictly worse
than the wiring loss `portMap` was made id-keyed to prevent. The **on-disk** form is still by name,
because no `PortId` appears anywhere in this format (dynamic pins replay as `{name, type}` and ids are
minted fresh in replay order), so name is the durable key and `PortId` the runtime identity — each
doing the job it is good at.

**The reserved pins have to exist, and `continue` had to learn from the Gate.** A loop's interior must
talk to the engine in both directions, and no group has ever needed to. Reserving them by *name* was
rejected on the same grounds as name-paired carries: a rename would silently turn a while loop into a
count loop. The sharper trap is `continue`'s empty state — "nobody wired it" and "the thing wired to
it was suppressed" are the same empty slot with opposite meanings, which is precisely the hazard
`GateNode::enable` hit on 2026-08-15. `Default{true}` resolves it the way that fix already proved: a
default seeds an input with **no incoming edge** and never fills a connected one that produced nothing,
so an unwired condition is transparent while a broken one suppresses.

**Gathering would make Loop and Map two ways to do one thing.** Mirroring an unpaired output as
`vector<T>` to accumulate across iterations is elegant, reuses `exitMap`'s gather verbatim, and would
be chosen by ADR-0014's own "the declaration is the mode" trick. It is refused because when the passes
are independent a map is already the right node, and when they are not, a scan of 10 000 iterations
silently materialises 10 000 images. Keeping the division sharp — *a map maps, a loop folds* — is worth
more than the expressiveness, and the escape is to wire the loop's output into a map.

**`iterations` exists because the bound creates an ambiguity that must not be silent.** Once every
loop is bounded, "converged at 7" and "hit the bound at 100 without converging" are the same event seen
from outside. Reporting the count makes the difference a **fact the graph reads** rather than a policy
the engine invents, and it costs one integer output. The alternative — every user who cares
reinventing the same counter inside the body — is the shape the 2026-08-15 param audit called out.

**Staging is the only lowering that keeps the standing rules, exactly as it was for the map.** A while
loop's trip count is genuinely unknowable in advance, so nothing but re-planning can work for it — and
a count loop gains nothing from a second lowering, because the carry makes the iterations sequential,
so unrolling N children into one plan buys no parallelism, forces all N to be prepared and retained up
front, and means two lowerings for one node kind. The tempting shape is a single `LoopExec` step that
iterates internally: it is by far the cheapest, since the interior plan would be built once and run N
times, and it violates three rules at once — a worker task would grow evaluation storage (ADR-0012), a
scheduler would run inside a task (ADR-0009), and fire-and-join goes with it. The cost of the honest
shape is `O(N × document)` planning, and the escape is plan caching, which WORK.md already defers.

**Reading a carry out before rebinding is safe because of a decision already made.** Iteration *k+1*
reuses iteration *k*'s child evaluation, so the coordinator copies each carry value out and binds it
back into the same tree. That is only sound because M5 slice 1 made `PortValue` payloads **shared and
immutable**: *"an earlier copy keeps the old payload and a recompute never disturbs a value another
slot is still reading"*. Without it, reuse would have needed a second child or a defensive deep copy
per carry per iteration.

**One retained child, because a loop has one result.** ADR-0014 accepted N× retained interior state on
the grounds that *"you cannot inspect element 7 if element 7's state was dropped"* — which holds
because a map's elements are results a user asked for. A loop's intermediate iterations are steps, not
results, and the count is a number the user types, so retaining them would turn ADR-0014's self-declared
sharpest cost into something a typo makes unbounded. It also keeps the loop entirely out of the
`EvalPath` / `PinKey` / breadcrumb surface that produced all ten of M5's bugs. The debugging gesture
for "show me iteration 3" is to set `count = 3` and look, which needs no machinery at all.

**A partial result that looks finished is worse than no result.** Delivering iteration *k−1*'s values
when the body suppresses would make a Gate a natural `break` and would never discard valid work. It is
refused because a genuinely broken body then produces a plausible answer nothing downstream can
distinguish from a converged one — the same silent-wrong-answer shape ADR-0014 rejected shortened
gathers for, and the same reject-rather-than-degrade call the image encoders and `UnnamedPinType`
already make. Keeping one concept per mechanism means a body that wants to stop early says so.

**Storing the pairing rather than the ports stores the fact rather than its consequence.** M5's rule
is that a group's own ports are never serialized, because derivation from the inner boundary is
*total*. ADR-0014 had to break that for the map because its mirroring is under-determined by one bit
per input pin, and it stored the **ports** so that what was written was a type rather than a flag. A
loop is under-determined by the **pairing** — a carried `Image` and an invariant `Image` are the same
type, so no declaration can encode it — but once the pairing is stored, seed / invariant / final /
last are all derivable, and the ports go back to being derived. Storing both would put a derivable
fact beside the underivable one, which is a second source that can disagree with the first.

## Consequences


**Amended 2026-09-08, after the first gui-mode use.** `continue` is a **POST-test**, and this ADR
never said so. It is a value the BODY produces, so it can only be read after a pass has run, and it
is asked about the pass that just ran. Three consequences the design implies but did not state:

- **A loop is a do-while.** With any positive `count` the body runs at least once; `count == 0` is
  the only pre-test, and it is the fold identity rather than a condition.
- **A condition on `index` is off by one against a C `for`.** `index < 4` runs **five** passes,
  because the last pass to run is index 4. That is self-consistent — "keep going while the pass that
  just finished was below 4" — but it is not what "while" suggests, and the word is used loosely
  throughout this document to mean the condition-driven MODE, not the timing.
- **`count` is the mechanism for "exactly N".** Conditioning on `index` re-implements it with that
  off-by-one built in. The condition exists for what a count cannot express: stop when the work stops
  changing, where one extra pass is meaningless.

And a trap worth naming, since it is the first thing a user reaches for: **a Gate must not be used to
end a loop.** `continue == false` stops it cleanly; a SUPPRESSED `continue` is an iteration failure
and the exit clears every output. That split is deliberate (see the `GateNode::enable` resolution
above) — it is what distinguishes "the loop finished" from "the fold broke" — but it makes the
obvious gesture the wrong one.

There is no pre-test available and this is not an omission: a pre-test would need the condition
computed outside the body, and the entire reason it lives inside is that it is computed FROM the
body's work.

- **`Node::evaluatesPerElement()` becomes an enum**, `interiorEvaluation() -> { Once, PerElement,
  PerIteration }`. Two booleans could express a nonsense state; an enum cannot, and `-Wswitch` then
  catches the next interior kind. It also drops a loop into `Evaluation::prepare`'s
  guaranteed-one-child arm and keeps the breadcrumb's element stepper off a loop crumb, both for free.
- **A frontier may now be raised repeatedly**, which contradicts `scheduler.h`'s current prose and the
  `PreparedMaps` comment that a map *"is deferred at most once per invocation"*. `PreparedMaps` becomes
  per-frontier staging state; the map's own behaviour is unchanged. *(Built 2026-09-05:
  `Scheduler::Staging`, a record per frontier ADDRESS — looked up through `Frontier`'s own equality —
  answering a **count of preparations** rather than membership, because a flag can say a frontier came
  back and only a count can say which time this is. Two things were measured rather than assumed: the
  map's bound really does pass through the new type — losing the record makes the staging loop spin
  forever — while the definition/evaluation halves of the address are **not** observable with only
  maps raising frontiers, since every frontier raised in a stage is prepared before the next, so two
  sharing a node id cannot diverge. The first case that buys them is a while loop inside a map whose
  rows stop at different iterations.)*
- **`expand` gains a step kind that both emits and defers.** An iterating loop contributes its
  interior steps *and* a frontier in the same stage, which no node has done before. Everything
  downstream of it is deferred exactly as it is for a map, so nothing at that level reads its `ends`.
  *(Checked at the code, 2026-09-05: this needs no seam of its own — a node may push steps, then
  `deferred.insert(id)` and set no `ends` entry, and the existing `downstreamOfDeferred` scan and
  both `runStages` break conditions already cope.)* **Amended at the build, 2026-09-06: it emits and
  defers only when the iteration will FINISH in that stage.** If the interior deferred anything of
  its own — a map sizing its children, a nested loop mid-fold — the loop must NOT re-raise, because
  the coordinator would then read carried outputs that do not exist yet, see them empty, and call
  the iteration failed. `expand` measures whether its recursive expansion raised a frontier and
  holds the loop back a stage; the interior's own frontier keeps the staging loop moving meanwhile,
  which is what makes this ADR's *"a map inside a loop costs two stages per iteration"* literal.
  Found by the nested tests failing on their first run, not by the design.
- **Planning is `O(N × document)` for an N-iteration loop.** Each stage rebuilds a plan. Acceptable
  for the counts a graph editor will see, and the named escape is plan caching.
- **The scheduler learns a loop through one new structural seam, `Node::iterationPorts()`** *(built
  2026-09-06; this ADR did not say how)*. It answers `bound`, `report`, `index`, `condition` and the
  carry pairing together, beside `innerGraph()` / `innerPin()` / `interiorEvaluation()`, so the
  execution layer still names no node class. A map needed no equivalent because split-or-broadcast
  follows from a port's own declared type; not one of a loop's five facts follows from anything.
- **Seeding a new iteration FORGETS the staging state inside the interior** *(built 2026-09-06; not
  anticipated here)*. One child evaluation is reused, so a map inside the body keeps the same
  frontier address every pass and would be expanded against the previous iteration's children — a
  silently wrong answer. It is needed for a nested loop too, which must start its own fold over on
  each outer pass. The termination bound therefore reads *"a map defers at most once per enclosing
  iteration"*, still bounded because iterations are.
- **`LoopExit` re-asks readiness and fold failure rather than remembering them** *(built
  2026-09-06)*, which is `exitMap`'s rule; and it is a routine of its own rather than `exitGroup`,
  which clears an output with no inner pin — exactly the loop's own `iterations`.
- **Only the last iteration is inspectable.** Stated as a cost, with `count = 3` as the gesture.
  *(Built 2026-09-06: a loop crumb says `[last iteration]` where a map crumb offers its element
  stepper, so the missing stepper reads as a decision at the one place a user would look for it.)*
- **A loop inside a map iterates in lockstep across elements**, because the staging loop handles every
  frontier in a stage together — so stages are bounded by the deepest loop's count, not by their sum.
  A map inside a loop costs two stages per iteration.
- **`syncGroupPorts` needs no exception for `count` / `iterations`** *in its removal phase*: it
  iterates `portMap()`, so a node-owned port that mirrors nothing is already invisible to it —
  verified at the build, along with the same property in `enterGroup`. But the *rename* and *add*
  phases each needed one, for the collision in the next bullet, as well as the reserved-pin skip.
- **A loop is the first group kind with ports of its OWN, so it is the first where an inner pin's name
  can COLLIDE with one** *(found at the build, 2026-09-05; not anticipated here)*. A map owns no
  ports, so this has never been possible, and `addInputLike` answers a duplicate with an **assert**.
  It is refused and reported by name, the same add-nothing-rather-than-degrade call
  `MapNode::exposePort` makes for an unliftable type — which that refusal now inherits, having been
  silent since M8. The collision arrives by **two** doors: the add phase, and the rename phase
  retitling a mirrored port onto a name the loop already owns, which would leave two same-named outer
  ports and make every name-addressed edge through them ambiguous on disk.
- **`GroupSync` gains `refused` (names, not a count), and it is deliberately not a `changed()`**
  *(build, 2026-09-05)*. A refusal is the only outcome here a user can act on, and acting needs to
  know which pin. But a refused pin is retried on every pass and a host syncs every frame, so counting
  it as a change would bump the node's recipe version continuously and force every evaluation to
  re-run forever: it describes a steady state, not a transition.
- **A carry whose inner pin is deleted is PRUNED, through a new `GroupNode::reconcileInterior()`**
  *(build, 2026-09-05 — this ADR did not settle it)*. The pairing is the one thing about an interior
  that is not derivable from it, and the Interface pane's ± can delete either half. The survivor then
  means exactly what an unpaired pin means — an invariant, or a last-iteration output — so derivation
  stays total and there is no broken state to represent. A virtual for the reason `exposePort` is one:
  the reconciliation gesture must not learn which kind it is holding.
- **A carry legitimately produces an outer input and an outer output of the same name.**
  `Node::hasPortNamed` is per-direction, so this is unambiguous — but it is the first node where it
  happens by design rather than by accident.
- **A loader needs a hook to establish an interior born with more than its boundary pair** *(built
  2026-09-06; this ADR assumed the constructor sufficed)*. `serialize::loadBody` gains a
  `prepareInterior` callback, invoked on the freshly constructed Graph before any node is seated —
  which also makes the reserved ids deterministic and turns a document naming a dynamic pin `index`
  into a reported skip rather than a duplicate-name assert. `LoopNode::establishReserved` is the one
  routine both the constructor and the loader call, because a fact computed two ways eventually
  disagrees with itself, and here it would do so silently.
- **A carry restored from a document goes through the same check the authoring gesture does** *(built
  2026-09-06)*. `pairCarry` records the pairing and `addCarry<T>` delegates to it, so the two cannot
  disagree about what a valid carry is. *(Extended at slice 6: a HOST has a registry KEY where
  `addCarry<T>` wants a type, since its "+ add carry" menu is `portTypeKeys()`. So there is a keyed
  `addCarry(typeKey, name)` too, and both run one `addCarryUsing` differing only by their adder — an
  unknown key must REFUSE, never quietly substitute a type, because a mistyped carry is not a visible
  break but one that binds the wrong payload at runtime.)* It refuses a reserved pin, a pin already half of another
  pairing, and a **type mismatch** — `Evaluation::bind` type-checks nothing, so a mismatched pair
  would bind the wrong payload into a slot at runtime.
- **Renaming a defaulted port stranded the param behind it** *(found at slice 5, fixed at its
  source)*. `Port::setName` claims a rename touches nothing structural; that stops being true once a
  port has a `Default`, because the param keeps the old name and a param is addressed by name on
  disk. `continue` is the first port in the tree that is both defaulted and renameable, so the trap
  stayed latent until this milestone. `Node::renamePort` now moves both.
- **Two new example nodes** — `ImageDifferenceNode` and `CompareNode` — exist to make the while
  vertical real. Without them the condition path would be built and never exercised. *(Built
  2026-09-06. `ImageDifference` is a MEASUREMENT, not a blend, so ADR-0003's op-class enforcement does
  not apply and nothing is converted to Linear — it compares the bytes it was given, which is what
  makes "the last pass produced the same picture" mean what a caller expects; differing size or format
  is refused rather than reconciled, and inside a loop that refusal is an iteration that failed.
  `Compare` carries the four INEQUALITIES and no equality: exact float equality would need an epsilon
  policy nothing asks for, and "close enough" is `Less` against a tolerance, which is the shape a
  convergence test has anyway. And the vertical needed no tolerance at all — a clamped Gaussian blur
  of an 8-bit image reaches an exact fixed point, so "blur until it stops changing" is literal and the
  threshold is 0.)*

## Deliberately unsettled

- ~~**A linked loop.**~~ **Refused 2026-09-09**, exactly as ADR-0014 now refuses the linked map —
  compose it instead: a loop whose interior holds a linked group. The argument is written up once,
  there.
- **Retaining per-iteration state.** Would make a loop's interior inspectable per pass and give the
  breadcrumb an iteration stepper. It needs a retention policy, which ADR-0012 refuses to have.
- **Plan caching across stages.** The optimisation that makes this lowering cheap. Deferred with its
  own invalidation questions, and no caller is measuring.
- **A scan output.** Refused above, not impossible: the machinery is `exitMap`'s gather.
- **Whether the interior should see `count`** as well as `index`, so a body can compute progress.
  Additive, and nothing asks for it.

**Not reopened by this ADR:** [ADR-0018](0018-frame-sequences-and-host-driven-rendering.md)'s
host-owned frame loop. A Loop node folds *within one evaluation* over data already in the graph; a
render folds over *frames*, and the host owns that loop because carrying processed frames would need a
graph-backed source whose decode re-enters the scheduler — the fire-and-join deadlock. The two are
different loops over different things.
