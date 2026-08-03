# A render layer may be consumed pre-transparent, not only post-transparent

**Status:** accepted

The [CompositorRenderLayer](../../scene/resources/compositor_render_layer.h) primitive draws its held-out
members into an engine-owned target and makes that target available to a consuming [CompositorEffect] at a
declared pipeline stage ([member stage]). This ADR records why that stage accepts **two** points —
`POST_TRANSPARENT` (the default) and `POST_OPAQUE` — rather than the single post-transparent point the v1
proposal shipped.

## Decision

`set_stage` honors `EFFECT_CALLBACK_TYPE_POST_OPAQUE` in addition to `EFFECT_CALLBACK_TYPE_POST_TRANSPARENT`,
and the Forward+ render path draws the held-out pass at whichever the layer declares:

- **`POST_TRANSPARENT` (default, post-resolve):** the held-out target is drawn *after* the transparent resolve.
  The consuming effect composites it over the fully rendered scene — the v1 behavior.
- **`POST_OPAQUE` (pre-transparent, post-resolve):** the held-out target is drawn *before* the transparent
  pass. A consuming effect that composites the target back into scene color at this point places the held-out
  result *underneath* the engine's own transparent geometry, so those transparents draw over it with the
  engine's normal alpha blend instead of being stamped over.

Both are post-opaque-*resolve*: the members are single-sample regardless. This is depth-test-only (members are
occluded by opaque geometry, do not write depth, and do not occlude one another or later transparents) — MSAA-off
scope, matching v1.

## Why

The consume point is an **ordering** choice, and ordering relative to the transparent pass is the load-bearing
property for a client that composites a held-out layer *into* scene color (rather than merely reading it). If
such a client resolves post-transparent, its composite is stamped over transparents the engine already drew; if
it resolves pre-transparent, the engine's transparents layer over it correctly and — because nothing renders
between the pre-transparent seed and resolve — the composited backdrop stays consistent with what the client
blended against. Exposing the stage as a declared knob lets the client pick the ordering it needs instead of the
primitive hardcoding one.

This is a **distinct motivation** from the "earlier stage" the interface design
([docs/render-to-compositor-interface-design.md](../render-to-compositor-interface-design.md) §2.5) originally
reserved: that reservation was for *pre-resolve* consumption to obtain MSAA'd held-out geometry. This ADR's
`POST_OPAQUE` is still post-resolve and delivers no MSAA change — it buys ordering, not sample count. The
pre-resolve MSAA path remains deferred.

## Consequences

- **Diverges from the shipped v1 proposal**, which offered `POST_TRANSPARENT` only, widening this fork's gap
  with the upstream proposal. Upstreaming the stage knob would still owe the pre-resolve MSAA path this change
  does not build.
- **Occlusion limit (accepted):** because the held-out pass does not write depth, a transparent spatially behind
  a `POST_OPAQUE`-consumed member shows through it. Fine while consumers intend their transparents to sit on
  top; a future depth-write option is the fix if a concrete case needs it.
- **Downstream consumers keep their own rationale.** Why a *particular* effect chooses `POST_OPAQUE` (its
  art-direction or compositing goal) belongs in that consumer's own repository, not in this engine primitive,
  which stays policy-free per the "engine carries zero downstream policy" invariant (spike ticket 11).
