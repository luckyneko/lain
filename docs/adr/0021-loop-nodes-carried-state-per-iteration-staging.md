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
  `Default{}` and an **optional** inner `continue : bool`. A count loop is the count alone; a while
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
  *add* phase skips them **by id, never by name**. `continue` carries `Default{true}`, so **unwired
  means the count decides** while **wired-and-suppressed stays empty and means the iteration failed**.
- **A loop folds; it never scans.** Each carry mirrors out as its **final** value, each unpaired inner
  output as its **last-iteration** value, plus one node-owned `iterations : int`. A map maps, a loop
  folds.
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
  map's.
- **Inline only.** A linked loop is deferred for the same reason a linked map is.

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

- **`Node::evaluatesPerElement()` becomes an enum**, `interiorEvaluation() -> { Once, PerElement,
  PerIteration }`. Two booleans could express a nonsense state; an enum cannot, and `-Wswitch` then
  catches the next interior kind. It also drops a loop into `Evaluation::prepare`'s
  guaranteed-one-child arm and keeps the breadcrumb's element stepper off a loop crumb, both for free.
- **A frontier may now be raised repeatedly**, which contradicts `scheduler.h`'s current prose and the
  `PreparedMaps` comment that a map *"is deferred at most once per invocation"*. `PreparedMaps` becomes
  per-frontier staging state; the map's own behaviour is unchanged.
- **`expand` gains a step kind that both emits and defers.** An iterating loop contributes its
  interior steps *and* a frontier in the same stage, which no node has done before. Everything
  downstream of it is deferred exactly as it is for a map, so nothing at that level reads its `ends`.
- **Planning is `O(N × document)` for an N-iteration loop.** Each stage rebuilds a plan. Acceptable
  for the counts a graph editor will see, and the named escape is plan caching.
- **Only the last iteration is inspectable.** Stated as a cost, with `count = 3` as the gesture.
- **A loop inside a map iterates in lockstep across elements**, because the staging loop handles every
  frontier in a stage together — so stages are bounded by the deepest loop's count, not by their sum.
  A map inside a loop costs two stages per iteration.
- **`syncGroupPorts` needs no exception for `count` / `iterations`**: its removal phase iterates
  `portMap()`, so a node-owned port that mirrors nothing is already invisible to it. Only the *add*
  phase changes, to skip the two reserved inner pins.
- **A carry legitimately produces an outer input and an outer output of the same name.**
  `Node::hasPortNamed` is per-direction, so this is unambiguous — but it is the first node where it
  happens by design rather than by accident.
- **Two new example nodes** — `ImageDifferenceNode` and `CompareNode` — exist to make the while
  vertical real. Without them the condition path would be built and never exercised.

## Deliberately unsettled

- **A linked loop.** One shared template iterated, exactly as ADR-0014 defers the linked map, and it
  would reuse `LinkedGroupNode::adoptInterior` the same way.
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
