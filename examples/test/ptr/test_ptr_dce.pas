program TestPtrDce;
{ Regression test (same class of bug fixed for array/record-array
  assignments - see docs/ROADMAP.md): new(p)'s runtime side effect
  (OP_NEW can abort on heap exhaustion) must never be silently dropped
  by dead-code elimination just because 'wasted' is otherwise unread.
  Without the has_heap_alloc_side_effect() DCE guard, this loop's
  new(wasted) calls would be wrongly swept away as dead, and the
  program would exit 0 (printing 'done') instead of aborting with a
  Runtime Error once the heap (MAX_HEAP_MEM = 4096 ints) is exhausted. }
type
    PInt = ^integer;
var
    wasted: PInt;
    i: integer;
begin
    for i := 1 to 5000 do begin
        new(wasted);
    end;
    writeln('done');
end.
