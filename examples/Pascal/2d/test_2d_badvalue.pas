program Test2DBadValue;
var grid: array[1..3, 1..4] of integer;
begin
    grid[1, 1] := 'oops';
end.
