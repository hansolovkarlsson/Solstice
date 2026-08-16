program TestNdArrayDce;
{ 'dead' is never read anywhere - but must not be eliminated by
  dead-code elimination, since its index's bounds check is an
  observable runtime side effect. Must still abort at 'dead[1, 1, 5]'. }
var
    dead: array[1..3, 1..3, 1..3] of integer;
begin
    dead[1, 1, 5] := 1;
    writeln('unreachable');
end.
