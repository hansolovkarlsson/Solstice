program TestNdArrayParam;
var
    cube: array[1..2, 1..2, 1..2] of integer;

procedure Inner(var c: array[1..2, 1..2, 1..2] of integer);
begin
    c[1, 1, 1] := 42;
end;

procedure FillCube(var c: array[1..2, 1..2, 1..2] of integer);
var
    x, y, z, n: integer;
begin
    n := 0;
    for x := 1 to 2 do
        for y := 1 to 2 do
            for z := 1 to 2 do begin
                n := n + 1;
                c[x, y, z] := n * 100;
            end;
    Inner(c); { forwarding an array-ref parameter through another call }
end;

procedure PrintCube(var c: array[1..2, 1..2, 1..2] of integer);
var
    x, y, z: integer;
begin
    for x := 1 to 2 do
        for y := 1 to 2 do
            for z := 1 to 2 do
                writeln(c[x, y, z]);
end;

begin
    FillCube(cube);
    PrintCube(cube);
end.
