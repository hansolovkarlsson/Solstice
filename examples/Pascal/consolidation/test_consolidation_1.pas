program TestConsolidation1;
type TPoint = record x, y: real; end;
var
    p: TPoint;
    grid: array[1..2, 1..2] of real;
    i, j: integer;
    s: string;
begin
    { real fields + math functions on them }
    p.x := 9.0;
    p.y := 16.0;
    writeln('sqrt(p.x) = ', sqrt(p.x):0:2);       { 3.00 }
    writeln('power(p.y, 0.5) = ', power(p.y, 0.5):0:2); { 4.00 }
    writeln('p.x + p.y = ', p.x + p.y:0:2);       { 25.00 }

    { 2D array with real elements, field-width printing }
    for i := 1 to 2 do
        for j := 1 to 2 do
            grid[i, j] := i * 1.5 + j;
    for i := 1 to 2 do begin
        for j := 1 to 2 do
            write(grid[i, j]:6:2);
        writeln;
    end;

    { string field: mutation + string functions together }
    s := 'World';
    writeln(copy(s, 1, 3));                        { Wor }
    s[1] := 'w';
    writeln(uppercase(s));                          { WORLD }
    writeln('length: ', length(s));                { 5 }
end.
