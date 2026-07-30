program Test2DBasic;
var
    grid: array[1..3, 1..4] of integer;
    i, j, total: integer;
begin
    for i := 1 to 3 do
        for j := 1 to 4 do
            grid[i, j] := i * 10 + j;

    for i := 1 to 3 do begin
        for j := 1 to 4 do
            write(grid[i, j], ' ');
        writeln;
    end;

    total := 0;
    for i := 1 to 3 do
        for j := 1 to 4 do
            total := total + grid[i, j];
    writeln('total = ', total);
end.
