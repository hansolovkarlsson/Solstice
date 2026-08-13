program TestDynArrayBadRange;
var
    arr: array of byte;
begin
    SetLength(arr, 2);
    arr[0] := 300;
end.
