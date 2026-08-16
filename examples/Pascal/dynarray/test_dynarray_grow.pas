program TestDynArrayGrow;
var
    arr: array of integer;
    i: integer;
begin
    SetLength(arr, 3);
    arr[0] := 10;
    arr[1] := 20;
    arr[2] := 30;
    SetLength(arr, 5);
    for i := 0 to High(arr) do
        write(arr[i], ' ');
    writeln;
    SetLength(arr, 2);
    for i := 0 to High(arr) do
        write(arr[i], ' ');
    writeln;
    writeln(Length(arr));
end.
