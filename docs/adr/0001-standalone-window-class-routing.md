# Standalone game windows get a fixed window class

On this local fork, a **standalone** game run — the Godot binary launched directly
to run a project, *not* as an editor F5/F6 debug child — reports the fixed window
class / Wayland `app_id` `godot-standalone` instead of the project's config name.
Every other window (editor, project manager, and editor-launched F5/F6 debug games)
is byte-for-byte unchanged.

## Why

Agents on this machine launch Godot from the CLI to run games; the user only ever
launches via the editor GUI (F5/F6). We want agent-launched game windows routed to a
throwaway workspace so they stop interrupting the user's own workspace, while the
user's own F5/F6 game runs stay put next to the editor.

The discriminator is **launch path**, detected intrinsically: an editor debug child
is spawned with `--editor-pid` (`editor/run/editor_run.cpp`), a standalone run is not.
So `CONTEXT_ENGINE` *without* `--editor-pid` == "standalone" == "route away".

`--editor-pid` is a *recognized* engine argument, so it is consumed during CLI parsing
and never stored in `OS::get_cmdline_args()` (`main.cpp` only keeps unrecognized/
forwardable args in the cmdline it hands to the display server). Detection therefore
reads the **raw kernel argv** from `/proc/self/cmdline` (Linux-only, which is fine —
these are the linuxbsd backends). The result is cached in a process-lifetime static.

A window-manager rule downstream (in `~/cfg`, not this repo) matches the class and
sends the window off:

```
# Hyprland
windowrule = workspace 10 silent, class:^godot-standalone$
```

X11 matches this against `res_class`; native Wayland against `app_id`. Both backends
are patched (`display_server_x11.cpp`, `display_server_wayland.cpp`); the user runs
XWayland today, so X11 governs now and Wayland is future-proofing.

## Considered and rejected

- **`GODOT_WM_CLASS` env var** set once in the agent environment, overriding the class
  for all contexts. Rejected: it requires configuring the agent's environment (and the
  agent resolving to the patched binary anyway), whereas the intrinsic `--editor-pid`
  signal needs **zero** agent-side config — more faithful to "the agent knows nothing."
- **Routing *all* `CONTEXT_ENGINE` windows** to the dumpster (no `--editor-pid` check).
  Rejected: it would exile the user's own F5/F6 games, breaking the edit→run→observe
  loop.

## Consequences

- Only affects the patched dev binary. Exported game builds are a separate, unpatched
  binary and keep their project-name class, so they are never mis-routed.
- The class is stable (`godot-standalone`) regardless of project name — that stability
  is the point, since it's what the window-manager rule matches.
- If an agent ever opens the **editor** GUI (currently it never does), that window is
  `CONTEXT_EDITOR` (class `Godot`) and would *not* be routed — a known boundary of the
  launch-path discriminator.
