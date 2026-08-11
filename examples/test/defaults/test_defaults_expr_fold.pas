program TestDefaultsExprFold;
{ default value is a constant EXPRESSION, folded at declaration time -
  not just a bare literal }
procedure Bar(a: integer; b: integer = 3 * 10);
begin
    writeln(a, ' ', b);
end;

begin
    Bar(1);  { 1 30 }
end.
