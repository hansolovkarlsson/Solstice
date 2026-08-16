program TestPointerBadCompareMismatch;

{ Comparing two SPECIFIC, unrelated pointer types directly (not through
  Pointer) still correctly fails - confirms the new Pointer-specific
  compatibility case didn't accidentally loosen the EXISTING "different
  declared pointer types can't compare" rule. }

type
    PInt = ^integer;
    PBool = ^boolean;

var
    a: PInt;
    b: PBool;

begin
    new(a);
    new(b);
    if a = b then writeln('should not compile');
end.
