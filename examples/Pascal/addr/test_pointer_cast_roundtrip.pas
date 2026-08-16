program TestPointerCastRoundtrip;

{ A specific pointer widened into Pointer, then cast back - confirms
  the cast and the original type genuinely agree (same address, same
  dereferenced value), not just "compiles". same_ok 7 }

type
    PInt = ^integer;

var
    p, q: PInt;
    g: Pointer;

begin
    new(p);
    p^ := 7;
    g := p;
    q := PInt(g);
    if q = p then writeln('same_ok');
    writeln(q^);
    dispose(p);
end.
