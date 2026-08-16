program TestPointerWiden;

{ Several different specific pointer types assigned into the same
  Pointer variable in turn - confirms the one-directional implicit
  widening in try_widen_for_assignment() works uniformly across plain
  assignment, argument-passing, AND a function's own return value (not
  just one of the 15 call sites). 5 true 5 }

type
    PInt = ^integer;
    PBool = ^boolean;

var
    a: PInt;
    b: PBool;
    g: Pointer;

function Identity(x: Pointer): Pointer;
begin
    Identity := x;
end;

begin
    new(a);
    a^ := 5;
    new(b);
    b^ := true;

    g := a;
    writeln(PInt(g)^);

    g := b;
    if PBool(g)^ then writeln('true') else writeln('false');

    g := Identity(a);
    writeln(PInt(g)^);
end.
