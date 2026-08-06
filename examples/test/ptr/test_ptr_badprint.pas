program TestPtrBadprint;
{ Standard Pascal defines no textual representation for a pointer value
  - write/writeln on one must be a Compile Error, not a printed raw
  heap offset. }
type
    PInt = ^integer;
var
    p: PInt;
begin
    new(p);
    writeln(p);
end.
