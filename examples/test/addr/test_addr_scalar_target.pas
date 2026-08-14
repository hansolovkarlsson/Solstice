program TestAddrScalarTarget;

{ @(p^) where p targets a plain scalar (not a record field) - the field
  offset is 0, so this is the trivial case: the resulting Pointer is
  just p's own value again. Cast back and dereference, value matches.
  42 }

type
    PInt = ^integer;

var
    p: PInt;
    g: Pointer;
    q: PInt;

begin
    new(p);
    p^ := 42;
    g := @(p^);
    q := PInt(g);
    writeln(q^);
end.
