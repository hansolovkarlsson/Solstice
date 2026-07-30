program Oob2;
var arr: array[1..5] of integer;
    i: integer;
begin
    i := 10;
    arr[i] := 42;
    writeln(arr[i]);
end.
