program AuditG;
var
    grid: array[1..2, 1..2] of real;
    i, j: integer;
begin
    grid[1,1] := 4.0;
    grid[1,2] := 9.0;
    grid[2,1] := 16.0;
    grid[2,2] := 25.0;
    for i := 1 to 2 do begin
        for j := 1 to 2 do
            write(sqrt(grid[i,j]):6:2);
        writeln;
    end;
end.
