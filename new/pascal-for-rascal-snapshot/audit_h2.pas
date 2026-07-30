program AuditH2;
type TPoint = record x, y: integer; end;
var p: TPoint;
begin
    p.x := 5;
    writeln(p);
end.
