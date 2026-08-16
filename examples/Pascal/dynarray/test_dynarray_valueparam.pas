program TestDynArrayValueParam;
var
    arr: array of integer;

procedure Modify(a: array of integer);
begin
    SetLength(a, 10);
    a[0] := 999;
end;

begin
    SetLength(arr, 3);
    arr[0] := 1;
    arr[1] := 2;
    arr[2] := 3;
    Modify(arr);
    writeln(Length(arr));
    writeln(arr[0]);
end.
