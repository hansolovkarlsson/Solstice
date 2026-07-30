program AuditJ;
procedure p;
var i, total, extra: integer;
begin
    readln(extra);
    total := 0;
    for i := 1 to 5 do
        total := total + i + extra;
    writeln(total);
end;
begin
    p;
end.
