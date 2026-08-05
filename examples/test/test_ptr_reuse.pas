program TestPtrReuse;
{ Regression test: dispose() must actually make a block available for
  reuse (a size-bucketed freelist - see vm_heap_freelist[] in vm.c), not
  just leak it. This loop calls new()/dispose() 10000 times - well past
  MAX_HEAP_MEM (4096 ints) - on a single-int pointer target. If dispose
  only leaked (bump-allocation only, no freelist), this would abort with
  "Heap storage exhausted" well before the loop finishes. Expected
  output: done }
type
    PInt = ^integer;
var
    p: PInt;
    i: integer;
begin
    for i := 1 to 10000 do begin
        new(p);
        p^ := i;
        dispose(p);
    end;
    writeln('done');
end.
