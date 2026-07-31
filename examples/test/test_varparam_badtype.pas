program TestVarParamBadType;
var
    x: real;

procedure Foo(var v: integer);
begin
end;

begin
    Foo(x);
end.
