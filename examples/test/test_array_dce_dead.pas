program TestArrayDceDead;
{ 'dead' is never read anywhere - but unlike a dead SCALAR assignment,
  this must NOT be eliminated by dead-code elimination: an array-element
  assignment's index carries an observable runtime side effect (an
  implicit bounds check), so removing it could silently hide a real
  out-of-bounds error. This program must still abort at 'dead[99]' even
  though 'dead' itself is otherwise completely unused. }
var
    used: array[1..3] of integer;
    dead: array[1..3] of integer;
    x: integer;
begin
    used[1] := 5;
    dead[99] := 999;
    x := used[1];
    writeln(x);
end.
