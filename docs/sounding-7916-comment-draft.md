# DRAFT — sounding comment for godot-proposals #7916

> **Status:** draft for the user to review and post *themselves*. **Do NOT post to `godotengine` on the user's
> behalf without explicit approval.** This is a low-cost sounding — a concrete proposal framed as a question —
> whose job is to get a *yes / no / redirect* from the #7916 owners (reduz / clayjohn / BastiaanOlij) on whether
> the transparent/held-out case lands *inside* #7916. Implementation is gated on that answer; no PR precedes it.
>
> Primary-source basis: `docs/research-gpu-driven-and-unification.md`,
> `docs/adversarial-review-narrowed.md`, `docs/adversarial-review-subviewport.md`.

---

## The comment (paste target)

**Sounding: can the transparent / held-out case land *inside* this proposal as one more pass configuration?**

I've been building a stylized 3D game whose look needs a `CompositorEffect` to consume geometry **shaded by a real
Godot material** (`StandardMaterial3D` / `ShaderMaterial`, `.gdshaderinc` and all), isolated into its own buffer,
occluded by the real scene, drawn in an order I control, and composited back in the same frame. Today a
`CompositorEffect` can't invoke a material at all, so this means hand-porting the material to GLSL and keeping it
synced forever. Before I build this out, I want to check the direction against *this* proposal rather than file
anything competing — because it reads to me as **the transparent/held-out case this thread has already been arguing
about** (darksylinc, thygrrr), not a separate feature.

**The key design decision I want to confirm:** I originally had membership be **per-instance** (opt an instance
into a held-out layer, so any `StandardMaterial3D` participates with no shader). Reading the FAQ — *"pass
assignment is material based … fully compatible with GPU driven rendering"* — and reduz's *GPU Driven Renderer*
gist (visible set sorted **by shader type** into one indirect draw list per material), I now think **per-instance
membership is wrong**: it would split each material's indirect draw batch under GPU-driven dispatch, which is
exactly what material-based assignment avoids. So I've dropped it and **kept membership material-based, in line
with this proposal.**

That leaves the held-out/transparent case expressible as **one more configuration of the pass primitive here**,
needing only **two things #7916 doesn't currently have**:

1. **A caller-controlled draw order within a pass** — a per-object order key, so a held-out/transparent pass draws
   in an author-declared sequence instead of depth sort. (This is the ask behind #3986 / #11251; it's an *order*
   axis, distinct from membership.)
2. **A held-out target the compositor seeds and composites in-frame** — the pass renders material-shaded geometry
   into a custom buffer (your `CUSTOM_BUFFER` surface), which a `CompositorEffect` seeds (clear / scene-color /
   a bound texture) and composites back at its stage. Occlusion is against the resolved scene depth.

**Questions for the owners:**

- Does the transparent / held-out case belong *inside* this proposal (as another pass configuration), or do you
  consider it out of scope?
- Given membership stays **material-based** (adopting your GPU-driven constraint), are those two additions — a
  **caller-order key within a pass** and a **held-out target the compositor seeds/composites in-frame** —
  acceptable extensions, or do they conflict with something in the design I'm not seeing?
- On the order key's *surface* specifically (veto-on-review): a **dedicated per-instance `int32` key** vs.
  **reusing the existing `sorting_offset`** with documented order semantics? I have it working as a typed
  `render_layer_order` (exact — no depth-bias overload, no float ULP cliff at large indices), but it's a small
  diff to fall back to reusing `sorting_offset` if you'd rather not grow the per-instance / inspector surface.
  Order must be per-*instance* (two instances of one material need different orders), so `render_priority`
  (material-level, 8-bit) can't serve it either way.

If this direction is welcome, I'm happy to do the implementation work; I'm asking first rather than opening a PR
against an unsettled design.

### How this answers points already raised in this thread

| Point raised | By | How this proposal addresses it |
|---|---|---|
| Pass assignment must stay **material-based** for GPU-driven dispatch | reduz (body + FAQ) | **Adopted as-is** — membership stays material-side; the two additions are an order key and a target, neither touches material dispatch. |
| Don't add special cases; **unify** pass-like features into one composable pass | csubagio (+17 / +7) | **Adopted** — this is one more configuration of *this* pass primitive, not a new render-list/flag/resource of its own. |
| Compositor shouldn't be opaque-only; needs to **render special geometry** (e.g. gun/hands in front) | darksylinc (+28), thygrrr | The **held-out/transparent target** is exactly that; author-ordered painter's draw is the correct model for the gun/hands case. |
| **4 texture slots too few**; hashed/identity names are friendlier | darksylinc (+28) | Targets addressed by **identity**, not a flat 4-slot index — moves toward this ask. |
| **Pass index should be overridable per-object** | QbieShay (+3) | Wanted, but per-object membership override splits the GPU-driven batch — so it's a **costed follow-up, not v1**; v1 keeps membership material-based. |
| You can already do this with **Texture2DRD** | clayjohn | Texture2DRD can't invoke a `ShaderMaterial`, and (per FuWan722) breaks with multiple viewports — it doesn't cover material-shaded held-out geometry. |
| Compositor should **broaden** toward Compatibility, not narrow | clayjohn | Forward+-first; same broadening trajectory this proposal is already on (Mobile/Compat as later work, see note). |

*Renderer scope:* Forward+-first. A deferred held-out pass into a separate target is a natural fit for Clustered
(resolve → post-transparent on resolved color/depth); Mobile's tile-based subpass chain needs its own design (it
already drops the subpass fast-path when a PRE/POST_TRANSPARENT effect is present), so Mobile/Compatibility are
follow-ups, not a claimed free mirror.

*Full analysis (interface, engine seams, the SubViewport/`Texture2DRD` workaround refutations, and known v1
limitations incl. the seed↔composite-op coupling and the held-out-depth/self-occlusion bound) is written up in
detail and linkable if useful.*

---

## Reviewer notes (NOT part of the pasted comment)

- **The credibility hook is the second paragraph** — conceding per-instance membership *because* we traced reduz's
  own gist is the single strongest signal that we understood the constraint, not just worked around it. Keep it.
- **Seed footgun (Attack #3) and depth self-occlusion (Attack #5)** are deliberately *not* rows — no participant
  raised them, they're post-greenlight v1 interface design, and surfacing them pre-yes/no invites a bikeshed.
  They're named once, neutrally, in the "full analysis" line so we're not concealing them.
- **QbieShay row is framed as understood-and-declined**, not as support for v1 — she asks for the membership
  axis (which pass an object is in), which is the thing we dropped; our caller-order addition is the *order* axis
  and is separate.
- **"Held-out target" / "custom buffer" wording** intentionally reuses #7916's own vocabulary so it reads as a
  configuration of their surface, not a parallel one.
- If the owners say "out of scope / wait," the fork (ADR-0001) remains the fallback — do not escalate to a PR.
