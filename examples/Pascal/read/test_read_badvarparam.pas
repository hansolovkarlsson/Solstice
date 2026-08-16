program TestReadBadVarParam;
procedure Foo(var x: integer; y: integer);
begin
    read(y, x);
end;

begin
end.
