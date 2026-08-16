program TestDefaultsRealWiden;
{ default '= 5' is an integer literal, widened to TYPE_REAL for a 'real'
  parameter - exactly like a caller-supplied integer argument would be }
procedure Foo(x: real = 5);
begin
    writeln(x:0:2);
end;

begin
    Foo;        { 5.00 }
    Foo(2.5);   { 2.50 }
end.
