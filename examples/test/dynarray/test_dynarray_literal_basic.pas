program TestDynArrayLiteralBasic;
var
    arr: array of integer;
    i: integer;
begin
    arr := [10, 20, 30];
    writeln(Length(arr));    { 3 }
    for i := 0 to High(arr) do
        write(arr[i], ' ');
    writeln;                  { 10 20 30 }
end.
