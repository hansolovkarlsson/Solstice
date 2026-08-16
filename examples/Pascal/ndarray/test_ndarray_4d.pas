program TestNdArray4D;
var
    a: array[1..2, 1..2, 1..2, 1..2] of integer;
    w, x, y, z, n: integer;
begin
    n := 0;
    for w := 1 to 2 do
        for x := 1 to 2 do
            for y := 1 to 2 do
                for z := 1 to 2 do begin
                    n := n + 1;
                    a[w, x, y, z] := n * 10;
                end;
    writeln(a[1, 1, 1, 1]);
    writeln(a[2, 2, 2, 2]);
end.
