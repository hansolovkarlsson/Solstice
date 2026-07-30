program NegBound;
var arr: array[-3..3] of integer;
    i: integer;
begin
    for i := -3 to 3 do
        arr[i] := i * 10;
    for i := -3 to 3 do
        writeln(arr[i]);
end.
