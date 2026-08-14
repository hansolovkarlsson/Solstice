program TestDynArrayLiteralArgReal;
procedure PrintAll(a: array of real);
var i: integer;
begin
    for i := 0 to High(a) do write(a[i]:0:1, ' ');
    writeln;
end;
begin
    PrintAll([1.5, 2.5, 3.5]);  { 1.5 2.5 3.5 }
end.
