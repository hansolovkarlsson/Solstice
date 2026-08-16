program TestForInArrayRefParam;
var
    a: array[1..4] of integer;
    i: integer;

procedure PrintAll(var arr: array[1..4] of integer);
var x: integer;
begin
    for x in arr do
        writeln(x);
end;

begin
    for i := 1 to 4 do a[i] := i * 100;
    PrintAll(a);
end.
