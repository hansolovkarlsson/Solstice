program TwoArrays;
var
    a: array[1..3] of integer;
    b: array[1..3] of integer;
    i: integer;
begin
    for i := 1 to 3 do begin
        a[i] := 100 + i;
        b[i] := 200 + i;
    end;
    for i := 1 to 3 do
        writeln(a[i], ' ', b[i]);
end.
