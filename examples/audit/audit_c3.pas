program AuditC3;
type TPoint = record x: integer; end;
var p: TPoint;
begin
    writeln(low(p.x));
end.
