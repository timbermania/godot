# [Build · seed] Real `TEXTURE` seed source (replace Step-5 CLEAR fallback)

Type: task
Status: open
Blocked by: 10

## Question

Ship the bound-`TEXTURE` seed source. **Critical path** — the FFT fold's sub/mix modes read `B` in place, so a
CLEAR seed is insufficient; the game supplies its display-space scene as a bound texture (ticket 03, D1). Step 5
shipped CLEAR only (`TEXTURE` warns and falls back). Detail: `plans/03-game-migration-plan.md` D1 + ticket 13.

- Add `seed_texture : Texture2D` to `CompositorRenderLayer` (only meaningful when `seed_source == TEXTURE`).
- At pass time, before drawing the held-out partition, **copy the bound `seed_texture` into the engine-owned
  target** (replacing the CLEAR seed for this source). Format/size must match the target (structural check per
  ticket 11).
- The seed must accept a **live / per-frame-updatable RD-backed** texture (the game rewrites it each frame in its
  Pass A), not just a static imported image — confirm the accepted `Texture2D` kind (e.g. `Texture2DRD`) and that
  the engine reads the current RID at pass time.
- `SCENE_COLOR` stays **out** (linear-HDR vs display-space; would leak the linear→display transform into core) —
  document as a v2 extension only.

**Done:** a `CompositorRenderLayer` with `seed_source = TEXTURE` + a per-frame-updated `seed_texture` renders its
held-out members composited over that texture's content (verified by RD readback), with no CLEAR fallback warning.

## Answer

<!-- filled on resolution -->
