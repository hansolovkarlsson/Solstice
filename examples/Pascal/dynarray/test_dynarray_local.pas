program TestDynArrayLocal;

function Sum(n: integer): integer;
var
    local: array of integer;
    i, total: integer;
begin
    SetLength(local, n);
    for i := 0 to n - 1 do
        local[i] := i + 1;
    total := 0;
    for i := 0 to High(local) do
        total := total + local[i];
    Sum := total;
end;

begin
    writeln(Sum(5));
    writeln(Sum(10));
end.
