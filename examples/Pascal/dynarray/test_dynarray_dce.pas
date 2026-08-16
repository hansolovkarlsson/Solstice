program TestDynArrayDce;
{ Regression test: a dynamic-array literal's own runtime side effects
  (the OP_NEW heap allocation, and each element's own range check for a
  byte/shortint/word element type - see wrap_range_check() in parser.c)
  must never be silently dropped by dead-code elimination just because
  the target variable is otherwise unread. Found while adding a bare
  dynamic-array typed constant: has_range_check()'s shallow check on the
  assignment's immediate child never looked inside a NODE_DYNARRAY_
  LITERAL's own element list, one level further down, to find the
  range-check nodes wrap_range_check() puts there per-element - fixed by
  teaching has_heap_alloc_side_effect() to also recognize a
  NODE_DYNARRAY_LITERAL itself (see optimizer.c). Without the fix, this
  would wrongly compile and exit 0 instead of aborting with a Runtime
  Error - 'wasted' is otherwise unread. }
var
    wasted: array of byte;
begin
    wasted := [300];
    writeln('done');
end.
