program TestDynArrayLiteralReassign;
var
    arr: array of integer;
begin
    SetLength(arr, 5);
    arr[0] := 999;
    arr := [1, 2];   { fresh literal reassigned over an already-allocated array }
    writeln(Length(arr));  { 2 }
    writeln(arr[0], ' ', arr[1]);  { 1 2 }
end.
