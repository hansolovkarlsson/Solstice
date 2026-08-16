program TestDynArrayBasic;
var
    arr: array of integer;
    i: integer;
begin
    SetLength(arr, 5);
    for i := 0 to Length(arr) - 1 do
        arr[i] := i * i;
    for i := 0 to High(arr) do
        write(arr[i], ' ');
    writeln;
    writeln(Length(arr));
end.
