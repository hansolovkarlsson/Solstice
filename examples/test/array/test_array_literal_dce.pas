program TestArrayLiteralDce;
{ Regression check (companion to examples/test/dynarray/test_dynarray_
  dce.pas, which found this exact class of bug for a DYNAMIC array
  literal): an unused fixed-size array's own literal assignment must
  still range-check its elements at runtime, not get silently dropped
  by dead-code elimination. Confirmed already safe here - sweep_dead_
  assignments()'s NODE_ASSIGN case never eliminates an is_array-typed
  target at all (a pre-existing guard, since an indexed array write
  already carries its own always-observable bounds-check side effect -
  see optimizer.c), and this feature's own per-element NODE_ASSIGN
  chain reuses that exact same is_array-typed target, so no new gap
  exists here the way one did for dynamic arrays. }
var
    wasted: array[1..1] of byte;
begin
    wasted := [300];
    writeln('done');
end.
