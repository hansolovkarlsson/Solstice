program TestConsolidation4;
var grid: array[1..2, 1..2] of real;
begin
    grid[1, 1] := 1.5;
    writeln(grid[5, 1]);
end.
