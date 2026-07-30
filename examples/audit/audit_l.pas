program AuditL;
type TPoint = record x: integer; end;
var p1, p2: TPoint;
begin
    p1.x := 1;
    p2.x := 1;
    if p1 = p2 then writeln('equal');
end.
