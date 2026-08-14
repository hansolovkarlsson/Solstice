program TestPointerBadWrite;

{ write(genericPtr) - rejected, matching every other pointer type: a
  pointer's value has no textual representation. }

var
    g: Pointer;

begin
    g := nil;
    write(g);
end.
