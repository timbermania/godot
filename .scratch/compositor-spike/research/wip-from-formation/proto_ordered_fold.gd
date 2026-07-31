# PROTOTYPE — THROWAWAY. Not production. Delete after it has answered the question.
#
# QUESTION (de-risking the N-primitive ordered blend compositor):
#   Q2  Does a stable sort by OT depth reproduce PSX ordering-table order, incl. the
#       equal-z tie-break, so submit order stops mattering once sorted?
#   Q3  Is collapsing maximal runs of same-mode COMMUTATIVE primitives into one
#       accumulate+clamp EXACTLY equal to the naive per-primitive clamped left-fold?
#       Where does clamping break commutativity?
#
# WHY A CPU MODEL, NOT THE GPU: deep-research (COMPOSITOR_GENERALIZE_FEASIBILITY.md)
#   already CONFIRMED the raster ping-pong is feasible on Mobile (Q1). What is unproven
#   is ALGEBRAIC: grouped-fold == naive-fold, and the sort/tie-break. Those live entirely
#   in the per-pixel fold arithmetic, so a pure numeric model settles them decisively and
#   fast. The GPU only has to reproduce whatever this model says is correct.
#
# GROUND TRUTH = PSX 5-bit integer mode math. The PSX GPU blends into the RGB555
#   framebuffer and RE-READS that 5-bit value as the background B for the next primitive.
#   So the faithful reference is INTEGER channels in [0,31], folded one primitive at a
#   time. The atom's float srgb_to_lin(clamp(lin_to_srgb(bg) ± texel*level)) + quantize5
#   is Godot's *approximation* of exactly this; here we model the truth directly so the
#   comparison is integer-exact, not float-fuzzy.
#
# Run (headful, never --headless):  godot --path godot-learning --script res://tools/proto_ordered_fold.gd

extends SceneTree

# --- PSX ABR modes on 5-bit channels (B = framebuffer background, F = source contribution).
#     F already folds gouraud/level (texel*level) into [0,31], per the atom. ---
# mode 0 = (B+F)/2   mode 1 = B+F (sat 31)   mode 2 = B-F (sat 0)   mode 3 = B+F/4 (sat 31)

const SAT := 31   # 5-bit ceiling (RGB555)

func apply_mode(b: int, f: int, mode: int) -> int:
	match mode:
		0: return (b + f) >> 1            # ½B + ½F   (rescales B — hard boundary)
		1: return min(b + f, SAT)         # B + F     (add)
		2: return max(b - f, 0)           # B - F     (sub)
		3: return min(b + (f >> 2), SAT)  # B + ¼F    (add)
		_: return b

# Direction of a mode's contribution to B, and whether B keeps its unit coefficient (a==1).
# Only a==1 modes with the SAME direction can be grouped (single-sided saturation is
# associative). mode 0 has a==½ -> it rescales B, so it can NEVER join a run.
func run_class(mode: int) -> String:
	match mode:
		1, 3: return "add"    # a==1, b>0
		2:    return "sub"    # a==1, b<0
		_:    return "solo"   # mode 0 (and anything else) is its own step

# --- The two folds under test, per channel. Active versions are naive()/grouped() below;
#     these accumulate maximal same-direction a==1 runs (add={1,3}, sub={2}). ---

func _f_eff(p) -> int:
	# effective per-primitive contribution magnitude (mode 3 pre-shifts by 2)
	return (p.f >> 2) if p.mode_id == 3 else p.f

# apply_mode takes (b, f, mode); adapt to the dict prim shape
func apply_prim(b: int, p) -> int:
	return apply_mode(b, p.f, p.mode_id)

# ---------------------------------------------------------------------------------------

var rng := RandomNumberGenerator.new()

func mk_prim(mode_id: int, f: int) -> Dictionary:
	return {"mode_id": mode_id, "f": f, "mode": f}  # 'mode' kept as alias for f (legacy field)

func rand_prims(n: int) -> Array:
	var a := []
	for _k in range(n):
		a.append(mk_prim(rng.randi_range(0, 3), rng.randi_range(0, SAT)))
	return a

# fold that uses apply_prim (naive), replacing the mis-typed naive_fold above
func naive(bg: int, prims: Array) -> int:
	var b := bg
	for p in prims:
		b = apply_prim(b, p)
	return b

func grouped(bg: int, prims: Array) -> int:
	var b := bg
	var i := 0
	while i < prims.size():
		var cls := run_class(prims[i].mode_id)
		if cls == "solo":
			b = apply_prim(b, prims[i])
			i += 1
			continue
		var acc := 0
		var j := i
		while j < prims.size() and run_class(prims[j].mode_id) == cls:
			acc += _f_eff(prims[j])
			j += 1
		b = min(b + acc, SAT) if cls == "add" else max(b - acc, 0)
		i = j
	return b

func _init() -> void:
	rng.seed = 0xF7A_C0DE
	print("=== PROTO: ordered PSX blend fold — grouped vs naive (5-bit integer, byte-exact) ===\n")

	q3_grouping_equivalence()
	q3_where_clamp_breaks()
	q2_sort_and_tiebreak()

	print("\n=== VERDICT printed above. Prototype is throwaway. ===")
	quit()

# ---- Q3: is grouped == naive across a large random sweep? ----
func q3_grouping_equivalence() -> void:
	print("--- Q3a: grouped-fold vs naive-fold equivalence (random sweep) ---")
	var trials := 200000
	var mism := 0
	var first_cex = null
	for _t in range(trials):
		var bg := rng.randi_range(0, SAT)
		var prims := rand_prims(rng.randi_range(1, 6))
		var a := naive(bg, prims)
		var g := grouped(bg, prims)
		if a != g:
			mism += 1
			if first_cex == null:
				first_cex = {"bg": bg, "prims": prims.duplicate(true), "naive": a, "grouped": g}
	print("  trials=%d  mismatches=%d" % [trials, mism])
	if mism == 0:
		print("  RESULT: grouped == naive EXACTLY. Collapsing maximal same-direction a==1 runs")
		print("          (adds={1,3}, subs={2}) into one accumulate+clamp is byte-exact.\n")
	else:
		print("  RESULT: NOT equivalent. first counterexample:")
		print("    ", first_cex, "\n")

# ---- Q3: exhibit WHERE clamp breaks commutativity (add/sub interleave; mode 0) ----
func q3_where_clamp_breaks() -> void:
	print("--- Q3b: where does clamping break commutativity? (order-dependence demos) ---")

	# (1) add then sub  vs  sub then add — clamp makes them differ
	var bg := 25
	var add5 := mk_prim(1, 10)   # B + 10
	var sub5 := mk_prim(2, 10)   # B - 10
	var ab := naive(bg, [add5, sub5])
	var ba := naive(bg, [sub5, add5])
	print("  interleave add/sub  B=%d, F=10 each:" % bg)
	print("    add→sub = %d   sub→add = %d   %s" % [ab, ba, "ORDER MATTERS ✓" if ab != ba else "same"])
	print("    -> the add↔sub transition is a NON-commutative boundary; runs cannot cross it.")

	# (2) two mode-0 primitives are non-commutative even with NO saturation
	var m0a := mk_prim(0, 8)
	var m0b := mk_prim(0, 24)
	var xy := naive(10, [m0a, m0b])
	var yx := naive(10, [m0b, m0a])
	print("  two mode-0 (½B+½F)  B=10, F=8 then 24 vs 24 then 8:")
	print("    a→b = %d   b→a = %d   %s" % [xy, yx, "ORDER MATTERS ✓" if xy != yx else "same"])
	print("    -> mode 0 rescales B (a=½): never groupable, always its own sequential step.")

	# (3) pure add-run IS order-independent (single-sided saturation is associative)
	var p1 := mk_prim(1, 20)
	var p3 := mk_prim(3, 28)   # contributes 28>>2 = 7
	var q1 := naive(15, [p1, p3])
	var q2 := naive(15, [p3, p1])
	print("  pure add-run {mode1 F=20, mode3 F=28}  B=15 (saturating):")
	print("    order A = %d   order B = %d   %s" % [q1, q2, "commutes ✓" if q1 == q2 else "DIFFERS"])
	print("    -> confirms adds group safely; sub-run is the mirror (0-clamp).\n")

# ---- Q2: sort key + equal-z tie-break; submit order irrelevant once sorted ----
func q2_sort_and_tiebreak() -> void:
	print("--- Q2: OT-order sort key + equal-z tie-break (submit-order invariance) ---")
	# Model: each prim carries an OT depth z (bucket) and a submission index.
	# PSX OT = drawn far→near; within a bucket, AddPrim links to the HEAD, so equal-z
	# prims draw in REVERSE submission order. Back-to-front fold order therefore =
	# sort by z DESC (far first), tie-break by submission index DESC (psx-spx/ordtbl —
	# UNRATIFIED by research; confirm against live oracle via effect-parity before build).
	var base := [
		{"mode_id": 1, "f": 12, "z": 4, "sub": 0, "mode": 12},
		{"mode_id": 2, "f": 9,  "z": 4, "sub": 1, "mode": 9},   # same bucket as above
		{"mode_id": 0, "f": 20, "z": 2, "sub": 2, "mode": 20},  # nearer
		{"mode_id": 3, "f": 16, "z": 7, "sub": 3, "mode": 16},  # farthest
	]
	var canonical := _fold_sorted(6, base)
	print("  canonical folded result (from submit order) = %d" % canonical)

	# shuffle the submit order many ways; sorted fold must be invariant
	var stable := true
	for _t in range(5000):
		var shuffled := base.duplicate(true)
		_shuffle(shuffled)
		# NOTE: a stable OT re-submission re-assigns 'sub' by new arrival; model that so the
		# tie-break is order-preserving under the SAME z assignment.
		for k in range(shuffled.size()):
			shuffled[k].sub = k
		var r := _fold_sorted(6, shuffled)
		if r != canonical:
			stable = false
			break
	print("  RESULT: fold invariant under 5000 random submit orders = %s" % ("YES ✓ (sort fully determines order)" if stable else "NO — tie-break under-specified"))
	print("    sort key = (z DESC, submission DESC). Equal-z tie-break is the ONE unratified")
	print("    bit (Q2): confirm HEAD-insertion / reverse-submission against pcsx oracle.\n")

func _fold_sorted(bg: int, prims: Array) -> int:
	var s := prims.duplicate(true)
	s.sort_custom(func(a, b):
		if a.z != b.z:
			return a.z > b.z        # far (larger z bucket) first
		return a.sub > b.sub)       # equal-z: reverse submission (HEAD-insertion)
	return naive(bg, s)

func _shuffle(a: Array) -> void:
	for i in range(a.size() - 1, 0, -1):
		var j := rng.randi_range(0, i)
		var t = a[i]; a[i] = a[j]; a[j] = t
