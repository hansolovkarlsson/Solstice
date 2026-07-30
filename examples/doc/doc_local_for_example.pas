program DocLocalForExample;

function sumTo(n: integer): integer;
var i, total: integer;
begin
    total := 0;
    for i := 1 to n do
        total := total + i;
    sumTo := total;
end;

begin
    writeln(sumTo(10));
end.
