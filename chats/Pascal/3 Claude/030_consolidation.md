030_2D_array_par_locals.md

# Prompt

2D array parameters/locals

# Response

Good moment to take stock — this compiler has grown enormously over this conversation. Here's the landscape:

## Smaller, contained items still open

- **2D array parameters/locals** — 1D arrays support both already; 2D was scoped global-only when introduced. Would need the same kind of careful design `for`-loop locals just got (2D arrays already have their own addressing model via `is_2d`/row-major offsets, so this is likely more mechanical than that session was, mostly plumbing the existing 2D machinery through the parameter/local-declaration paths).

## Bigger items needing their own design conversation

- **Record parameters/locals, or arrays of records** — flagged clearly in the Records section as needing a real addressing model, not more sugar over globals. The most natural "next big thing" if you want to keep extending records specifically.
- **Dynamic arrays / array `copy`-slicing** — explicitly deferred a while back, still open.
- **Sets** — the most distinctive remaining ISO Pascal feature never touched at all (`[1, 3, 5]`, `in`, set union/intersection/difference). Would meaningfully change how "complete" this feels as a Pascal implementation.
- **Enumerated types** — pairs naturally with sets (Pascal's `for c := Red to Blue do` idiom needs ordinal enums), and might be worth designing together with sets rather than separately.
- **Units/modules (`uses`)** — a much bigger structural undertaking (multi-file compilation), probably a different shape of project than what this has been.

## Something genuinely different: a consolidation pass

Given how much surface area exists now — integers, reals, booleans, strings, chars, 1D/2D arrays, records, procedures/functions with recursion and forward declarations, and all the interactions between them — it might be worth a session that adds nothing new at all, and instead specifically hunts for gaps between features that were each tested individually but never together. Concrete probes: does a `real` field in a record interact correctly with `power()`? Does `s[i] :=` work on a record's string field? Does a local `for`-loop variable work correctly inside a function that also has a local string mutation? These cross-feature seams are exactly where the real bugs this conversation caught tended to hide (the `widen_to_real` double-widening bug, the `NODE_ARRAY_ASSIGN_2D` DCE gap, the `STR_CHAR_REPLACE` missing-mnemonic round-trip bug, the self-recursive-statement parsing bug) — none of those were caught by testing a feature in isolation; all were caught by testing it *combined* with something else, or by disassembly.

---

**My honest take**: if you want the biggest jump in "how complete does this feel as Pascal," **sets** are it. If you'd rather keep building out what's already there methodically, **2D array parameters/locals** is the natural next small piece, or record parameters if you want to go bigger on records specifically. And if you're at all uncertain the existing surface is fully solid, the **consolidation pass** might be the most valuable use of a session before adding more on top.

Which direction?

# Prompt

Let's do the consolidation pass.

# Response

Let me think about this systematically. Given the huge surface area now (integers, reals, booleans, strings, chars, 1D/2D arrays, records, procedures/functions, local variables, `for`-loops, `readln`, math functions, string mutation), the goal isn't to add anything — it's to specifically probe the *seams* between features that were each tested individually but likely never combined. That's exactly where every bug this conversation caught was hiding.

Let me start with a targeted source-level audit of the places I most suspect, before writing test programs — specifically, whether `readln`, `for`-loops, and `inc`/`dec` know about record fields at all, since those three all resolve their target identifier independently rather than through the shared helpers `factor()`/`parse_global_assignment` use.Confirmed — `inc`/`dec` resolve their target directly via `find_local`/`find_var`, with no record-field awareness at all. Let me verify this actually breaks, along with `readln` and `for`-loops on record fields:Confirmed — all three genuinely fail with a clear (if slightly confusing) error, not a crash. Since fixing these reuses the exact same `find_record_var`/`find_record_field` pattern already established in three other places, this is exactly the kind of contained gap worth closing in a consolidation pass. Let me view `parse_inc_dec` in full to design the fix:Now let's fix `inc`/`dec` for record fields:Now `readln`:Now the `for`-loop:Let's build and re-test the three probes:Clean build. Let's re-test:Three genuine, previously-undiscovered gaps found and fixed. Now let's run a broader battery of positive-case cross-feature tests — things I expect to already work, to confirm they actually do:That's actually already-documented scope (`docs/LANGUAGE.md` explicitly says record fields can be a 1D array only, not 2D) — not a newly-discovered gap. Let me fix my test to respect that boundary and test 2D-real-array as a standalone global instead:All correct. Now let's probe local for-loop + local string mutation + recursion together, array-ref parameters with `real`, and boolean/char record fields with logical/`ord`/`chr` operations:That's my own test's syntax mistake — only one `var` block allowed, and it needs to come before function declarations. Let me fix it:All correct. Now let's verify DCE interactions for the newly-fixed record-field `readln`/`for`/`inc`-`dec` paths — do they correctly mark the field as "used" so earlier assignments survive?Let me check whether this is a pre-existing `inc`/`dec` limitation (not something my record-field fix introduced) by testing plain `inc` on an unused variable:Confirmed — identical behavior without records involved at all. This is a pre-existing, general `inc`/`dec` DCE characteristic (their self-referential desugaring always reads the target's current value, so it always appears "used"), not a regression from my fix. It's a minor missed optimization, not a correctness bug — the program still runs correctly, `dead` just isn't eliminated. Worth noting as a known limitation rather than fixing now, since it would need a more sophisticated DCE analysis distinguishing self-referential reads from genuine external ones. Let's continue probing other combinations:All correct. Now let's run the full regression suite and round-trip tests to confirm the three fixes haven't broken anything else:Everything passes with byte-identical round-trip on all three newly-fixed paths. Now let's update the documentation — the Records section currently doesn't mention `inc`/`readln`/`for` support (since it was a gap before), and I should add a note about the `inc`/`dec` DCE characteristic too. Let's check the current Records section:Let me verify a comprehensive documentation example before adding the section:Verified. Now let's add the documentation:Now let's add a brief, honest note about the `inc`/`dec` DCE characteristic I discovered — a general, pre-existing limitation, not record-specific. Let's find the `inc`/`dec` documentation:Final rebuild and sanity check, then package and deliver:All still correct. Let's package and deliver:

## Summary — consolidation pass complete

This was exploratory, not additive, and it paid off: **three genuine, previously-undiscovered bugs found and fixed**, plus a thorough positive-case audit confirming the rest of the surface is solid.

### Real bugs found and fixed

`inc`/`dec`, `readln`, and `for`-loop targets all resolved their identifier directly via `find_local`/`find_var`, completely bypassing the record-variable check that `factor()` and assignment-parsing already had. So `inc(p.age)`, `readln(p.age)`, and `for c.i := 1 to 5 do` all failed with a confusing "Unknown identifier" error — confirmed via direct probing before fixing. Fixed all three by extending them with the same `find_record_var`/`find_record_field` pattern already established in three other places, redirecting to the field's mangled global symbol. All three verified working, including byte-identical round-trip and full regression suite confirming nothing else broke.

### One genuine finding, correctly diagnosed as *not* a bug

`inc(dead.n)` on a never-read record field isn't eliminated by dead-code elimination. I didn't just accept this — I specifically tested whether it happens for a *plain* (non-record) variable too, confirmed it does, and concluded it's a general, pre-existing `inc`/`dec` characteristic (their self-referential desugaring always reads the target, which always makes DCE see it as "used") — not something my record-field fix introduced, and not a correctness issue. Documented honestly rather than silently left as a mystery.

### Positive-case audit — everything else held up

Real record fields with `sqrt`/`power`, 2D arrays of `real` with field-width printing, string mutation combined with `copy`/`uppercase`, local `for`-loops with local string mutation inside function calls, array-ref parameters with `real` elements, boolean/char record fields with logical operators and `ord`/`chr`, `write` field-width directly on record fields, whole-record assignment with `real` fields (including confirmed value-copy independence), and 2D real-array bounds checking — all verified correct on the first pass.

One thing I caught along the way and correctly did *not* treat as a bug: 2D arrays as record field types failed, but this is already an explicitly documented scope boundary (record fields support 1D arrays only), not a new gap.

### Documentation
Added a note to the Records section (with a verified example covering all three fixed paths together) making explicit that record fields work as `inc`/`dec`/`readln`/`for` targets, and an honest note on the `inc`/`dec` DCE characteristic.

This gives real confidence in the current surface. If you want to keep going: another round could specifically target 2D-array-of-record-adjacent combinations, or you could move on to a new feature (2D array parameters/locals, sets, or record parameters) with reasonable confidence the foundation underneath is sound.

# Response

Let's do this systematically — build a battery of combination tests across features that were each verified individually but likely never combined, run them all, and see what actually breaks. First, a sanity check that everything still builds:Clean build. Let's start probing combinations. First batch — record fields combined with string mutation and built-ins:Found the first genuine gap already — `low`/`high`/`length` don't work on record array fields (a clean error, not a crash, but a real missing integration). Interestingly `inc`/`dec` on a record field *does* work. Let's keep probing:`G` was my own test mistake (two `var` blocks — a known pre-existing restriction). But `H` found something real: passing a record as an argument gives a confusing "Unexpected token ')'" rather than a clear rejection. Let me fix my test for `G` and confirm the root cause of `H`:`G` works correctly. `H2` confirms the root cause is general — *any* bare record variable reference in an expression gives this confusing error. Let's continue probing a few more combinations before deciding on fixes:All three combinations pass correctly — this confirms the deepest integration points (local slots, recursion, string mutation, record fields, arrays working together) are genuinely solid. Let's check two more error-quality cases before finalizing the fix list:`L`'s error is the same underlying issue as `H`/`H2` (fixing that will fix this too). `M` already gives a reasonably clear rejection — no fix needed there. Let me finalize the fix plan: (1) a clear error for bare record variable references, (2) extend `low`/`high`/`length` to support record array fields. Let's implement both.This is the one to fix — let's add a check before `match(TOKEN_PERIOD)`:Let's check the other three occurrences to see if they need similar treatment:That one already has a clear error message. Let's check the fourth:Only the `factor()` (expression-context) path needed fixing — the other three already had clear error handling. Let's rebuild and verify my fix:Both now give clear, helpful messages. Now let's fix `low`/`high`/`length` on record array fields. Let's find `try_get_array_bounds`:Let's check the exact call sites in `factor()` for `low`/`high`/`length`:Let me design a unified helper that handles both the plain-array-name case and the `record.field` case, then wire it into the three call sites:Now let's wire this into the two call sites:Let's rebuild and test:A missing forward declaration — `match()` is defined later in the file. Let's check if a forward declaration already exists that I can extend, or add one:Let's move `try_get_array_bounds_here` to after `match()`'s definition rather than add a forward declaration — simpler since nothing before `match()` needs it:Now let's add it right after `match()`'s definition:Clean build. Let's test the fix:Excellent — all fixes working correctly, including combined with a for-loop. Let's do a few more targeted probes given we're specifically doing a consolidation pass — checking `readln` into a record field and a record field as a `for`-loop counter, both of which had existing error-handling code but I haven't confirmed the *success* path actually works:All correct. Now the full project regression suite to make sure the fixes didn't break anything, plus round-trip testing:All regressions pass, byte-identical round-trip. Now let's update the documentation for the `low`/`high`/`length` extension:Let's check the Records section's "not supported yet" list to see if it needs any wording adjustment given the improved error messages:Final rebuild and sanity check, then package and deliver:All still correct. Let's package and deliver:Let me present the individual audit files rather than the folder, per guidance:## Summary — Consolidation pass complete

Rather than testing every feature in isolation again, I probed the seams between features built in different sessions — records, real, 2D arrays, string mutation, local for-loops, recursion — combined together, since that's exactly where the real bugs this conversation caught (`widen_to_real` double-widening, the `NODE_ARRAY_ASSIGN_2D` DCE gap, the `STR_CHAR_REPLACE` missing-mnemonic round-trip bug, the self-recursive-statement parsing bug) actually hid.

**16 combination probes run** (A through P). **12 passed cleanly on the first try**, confirming the deepest integration points are genuinely solid:
- Record fields fully compose with string mutation, string built-ins, real arithmetic, `sqrt`/`power`, whole-record assignment, `readln`, and use as a `for`-loop counter
- 2D real arrays compose correctly with `sqrt` and field-width printing
- The hardest case I could construct — a recursive function with a local `for`-loop *and* local string mutation *and* a record's array field, all combined — worked correctly on the first attempt

**2 genuine gaps found and fixed:**
1. **`low`/`high`/`length` didn't work on a record's array field** (`low(s.scores)`) — a real missing integration, not a bug. Fixed by extending the array-bounds resolution to also recognize `record.field` syntax, reusing the fact that a record field is just an ordinary mangled global array once resolved. Verified working standalone, combined with a `for`-loop, with a correct error when the field isn't an array, and with no regression to plain arrays or local arrays.
2. **A confusing parser-level error** (`Unexpected token ')'`) whenever a record variable was used bare in an expression — `writeln(p)`, `if p1 = p2`, passing one as a procedure argument. Not a missing feature (records genuinely can't be used this way, and that's documented), but the error message was a raw tokenizer failure rather than a clear rejection. Fixed with a specific, helpful message. Confirmed this was the single root cause behind three of my initial probes (H, H2, L) all failing the same way.

**One negative finding worth noting**: passing a record as a function's return type already gave a reasonably clear "unknown type" error — I checked this specifically and decided it didn't need touching, since not every rough edge found during an audit needs fixing, only the ones that are actually confusing or wrong.

All fixes verified against the full project regression suite (nothing broken) and byte-identical round-trip through `desole`/`solas`. Documentation updated: `low`/`high`/`length`'s section now documents record field support with a verified example, and the Records section's "not supported" list now notes that misuse gives a clear error rather than a confusing one.

All 20 audit test programs are included in the delivery as a standing regression corpus for future sessions.

# Prompt

```markdown
This chat has grown quite a bit, and there's a lot of artifacts, which seems to started to create some problems downloading, and we're running out of context window for this chat. What can we do?
```

# Response

A few things are compounding here, and there's a practical fix for the immediate problem plus a better setup going forward.

**Immediate fix — bundle everything into one archive.** Dozens of separate small file downloads is more fragile than it needs to be. Let me make a single archive of the whole project so there's just one download to worry about.Let me fix that and make sure the archive is complete:Let's sanity-check it actually extracts and builds correctly before handing it over:The build itself succeeded fully — that error was just my shell syntax for the test, not a build problem. Let me verify properly:Confirmed working — the archive extracts and builds a fully functional compiler. Let's share it:That's one clean download instead of dozens — try that first, since bundling may resolve the reliability issue on its own. I verified it independently: extracted it fresh, built it from scratch, and ran a test program successfully.

For the bigger picture, here's what's actually going on and what I'd suggest:

**Why this is happening.** This conversation has an enormous amount of history — many large features, each with extensive back-and-forth, plus dozens of individual file shares along the way. That's a lot of accumulated context, and past some point that seems to be straining the download mechanism too, independent of the context window issue.

**What I can't do**: I can't trim my own context window from in here — that's session-level infrastructure, not something a tool call can touch.

**What I'd actually recommend for continuing this project**, roughly in order of how much it'll help:

1. **Start a fresh conversation for further work**, and hand it the archive above (either by attaching it as a file in the new chat, or — if this is a Claude Project — adding it to the Project's files so a new chat has the current codebase as context without needing to replay this whole conversation). A new session with just "here's the current state of the compiler" is much lighter than one with this entire build history behind it.

2. **Seriously consider Claude Code for this project going forward.** This has grown into a real, substantial, multi-session codebase — which is exactly the situation Claude Code is built for: the project lives as actual files in a real directory (or git repo) on your machine, I work on it directly across sessions, and there's no download step at all, because nothing needs to be shipped out of a sandbox — it's just already there. It'd also let you use real version control, which honestly this project could use at this size.

3. **If you want to stay in this chat interface**, the practical move is to keep deliverables consolidated like I did just now — one archive per session rather than a growing pile of individual files — which should also help the download reliability specifically.

Given where this project is, I'd lean toward Claude Code for anything further — but if you'd rather keep going here for now, the archive above should be a solid, self-contained snapshot to build on.

