program TestAddrBadPlainVar;

{ @x for an ordinary variable - a clear compile-time rejection, not a
  crash or a silently wrong value. This VM's ordinary variables don't
  live in the same addressable heap pointers do. }

var
    x: integer;
    g: Pointer;

begin
    g := @x;
end.
