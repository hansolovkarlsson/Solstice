program Test2DBadIndex;
var grid: array[1..3, 1..4] of integer; x: integer;
begin
    x := grid[1, 'a'];
    writeln(x);
end.
