program TestNdArrayBadIndex;
var
    a: array[1..3, 1..3, 1..3] of integer;
    x: integer;
begin
    a[1, 1, 1] := 1;
    x := a[1, 1, 1];
    writeln(x);
    a[1, 1, 5] := 2;
end.
