program AuditO;
type TCounter = record i: integer; end;
var c: TCounter; total: integer;
begin
    total := 0;
    for c.i := 1 to 5 do
        total := total + c.i;
    writeln('total: ', total);
end.
