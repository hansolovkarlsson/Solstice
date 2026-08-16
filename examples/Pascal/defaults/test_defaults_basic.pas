program TestDefaultsBasic;
procedure Foo(x: integer; y: integer = 10);
begin
    writeln(x + y);
end;

begin
    Foo(5);      { y defaults to 10 -> 15 }
    Foo(5, 20);  { y explicit -> 25 }
end.
