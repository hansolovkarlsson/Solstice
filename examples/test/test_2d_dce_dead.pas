program Test2DDceDead;
var
    used: array[1..2, 1..2] of integer;
    dead: array[1..2, 1..2] of integer;
    x: integer;
begin
    used[1, 1] := 5;
    dead[1, 1] := 999;  { never read anywhere - should be eliminated }
    x := used[1, 1];
    writeln(x);
end.
