program TestVarParamBadFor;

procedure Foo(var x: integer);
begin
    for x := 1 to 5 do
        writeln(x);
end;

begin
end.
