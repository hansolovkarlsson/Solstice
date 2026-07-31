program TestVarParamBadArray;
var
    arr: array[1..3] of integer;

procedure Foo(var v: integer);
begin
end;

begin
    Foo(arr[1]);
end.
