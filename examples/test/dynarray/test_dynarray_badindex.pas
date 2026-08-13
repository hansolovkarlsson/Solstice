program TestDynArrayBadIndex;
var
    arr: array of integer;
begin
    SetLength(arr, 3);
    writeln(arr[5]);
end.
