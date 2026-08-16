program TestDynArrayVarParam;
var
    arr: array of integer;

procedure Grow(var a: array of integer; n: integer);
begin
    SetLength(a, n);
    a[n - 1] := 42;
end;

begin
    SetLength(arr, 2);
    arr[0] := 1;
    arr[1] := 2;
    Grow(arr, 5);
    writeln(Length(arr));
    writeln(arr[0], ' ', arr[1], ' ', arr[4]);
end.
