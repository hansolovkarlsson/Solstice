program TestDynArrayLiteralArgEmpty;
function Sum(a: array of integer): integer;
var i, s: integer;
begin
    s := 0;
    for i := 0 to High(a) do s := s + a[i];
    Sum := s;
end;
begin
    writeln(Sum([]));  { 0 }
end.
