program TestDefaultsMultiple;
procedure Bar(a: integer; b: integer = 2; c: integer = 30);
begin
    writeln(a, ' ', b, ' ', c);
end;

begin
    Bar(1);        { 1 2 30 }
    Bar(1, 5);     { 1 5 30 }
    Bar(1, 5, 9);  { 1 5 9 }
end.
