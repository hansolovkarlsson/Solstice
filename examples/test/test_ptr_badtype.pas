program TestPtrBadtype;
{ Assigning a pointer of one declared type to a pointer of a different
  declared type must be a Compile Error, even if they happen to target
  the same underlying type - this compiler uses name equivalence for
  pointer types (matching Pascal convention), not structural. }
type
    PA = ^integer;
    PB = ^integer;
var
    a: PA;
    b: PB;
begin
    new(a);
    b := a;
end.
