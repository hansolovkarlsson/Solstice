program TestPointerBadNew;

{ new(genericPtr) - rejected: Pointer has no target type, so there's no
  allocation size to use. Falls out for free from the existing
  is_pointer_type() check new() already has (Pointer sits deliberately
  outside that bounded range). }

var
    g: Pointer;

begin
    new(g);
end.
