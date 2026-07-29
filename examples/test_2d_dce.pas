program Test2DDce;
var
    grid: array[1..2, 1..2] of integer;
    x: integer;
begin
    grid[1, 1] := 42;   { only ever read via 2D access below - must survive DCE }
    x := grid[1, 1];
    writeln(x);
end.
