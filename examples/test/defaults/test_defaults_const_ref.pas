program TestDefaultsConstRef;
const Limit = 42;
procedure Foo(x: integer = Limit);
begin
    writeln(x);
end;

begin
    Foo;      { 42 }
    Foo(1);   { 1 }
end.
