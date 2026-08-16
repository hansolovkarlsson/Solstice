program TestDynArrayBadAssign;
var
    arr: array of integer;
begin
    SetLength(arr, 3);
    arr[0] := 'not an integer';
end.
