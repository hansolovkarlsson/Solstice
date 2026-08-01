program TestNdArrayLocal;
procedure P;
var
    cube: array[1..2, 1..2, 1..2] of integer;
    x, y, z, n: integer;
begin
    n := 0;
    for x := 1 to 2 do
        for y := 1 to 2 do
            for z := 1 to 2 do begin
                n := n + 1;
                cube[x, y, z] := n;
            end;
    for x := 1 to 2 do
        for y := 1 to 2 do
            for z := 1 to 2 do
                writeln(cube[x, y, z]);
end;
begin
    P;
end.
