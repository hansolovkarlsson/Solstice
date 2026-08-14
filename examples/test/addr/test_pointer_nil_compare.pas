program TestPointerNilCompare;

{ Pointer = nil, <> nil, and = a specific pointer type holding the same
  value - all through the extended pointer-comparison special case.
  g_nil_ok g_notnil_ok g_eq_p_ok }

type
    PInt = ^integer;

var
    p: PInt;
    g: Pointer;

begin
    g := nil;
    if g = nil then writeln('g_nil_ok');

    new(p);
    g := p;
    if g <> nil then writeln('g_notnil_ok');
    if g = p then writeln('g_eq_p_ok');

    dispose(p);
end.
