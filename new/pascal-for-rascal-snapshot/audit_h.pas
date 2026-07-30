program AuditH;
type TPoint = record x, y: integer; end;
var p: TPoint;
procedure show(a: integer);
begin
    writeln(a);
end;
begin
    p.x := 5;
    show(p);
end.
