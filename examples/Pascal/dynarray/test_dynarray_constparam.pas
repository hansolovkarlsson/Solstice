program TestDynArrayConstParam;

procedure Foo(const a: array of integer);
begin
    SetLength(a, 5);
end;

var
    arr: array of integer;
begin
    SetLength(arr, 2);
    Foo(arr);
end.
