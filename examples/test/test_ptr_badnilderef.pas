program TestPtrBadnilderef;
{ Dereferencing a nil pointer must be a clean Runtime Error ("Nil
  pointer dereference"), not a crash. }
type
    PInt = ^integer;
var
    p: PInt;
    x: integer;
begin
    p := nil;
    x := p^;
end.
