program TestVarParamBadExpr;
var
    x: integer;

procedure Foo(var v: integer);
begin
end;

begin
    Foo(x + 1);
end.
