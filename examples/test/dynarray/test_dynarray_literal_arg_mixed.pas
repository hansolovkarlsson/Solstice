program TestDynArrayLiteralArgMixed;
function Combine(prefix: string; a: array of integer; suffix: string): string;
var i: integer;
    r: string;
begin
    r := prefix;
    for i := 0 to High(a) do r := r + IntToStr(a[i]);
    r := r + suffix;
    Combine := r;
end;
begin
    writeln(Combine('[', [1, 2, 3], ']'));  { [123] }
end.
