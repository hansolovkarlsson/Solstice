program TestAddrBadPointerItself;

{ @p (the pointer VARIABLE's own storage slot, as opposed to what it
  points to) - also rejected. p itself lives in vm_vars[], not
  vm_heap_mem[] - only what p points AT is heap-addressable. }

type
    PInt = ^integer;

var
    p: PInt;
    g: Pointer;

begin
    new(p);
    g := @p;
end.
