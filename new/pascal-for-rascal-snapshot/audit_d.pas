program AuditD;
type TPoint = record x: integer; end;
var p: TPoint;
begin
    p.x := 5;
    inc(p.x);
    writeln(p.x);
end.
