program AuditF;
type TCircle = record radius: real; end;
var a, b: TCircle;
begin
    a.radius := 2.5;
    b := a;
    writeln(b.radius);
    a.radius := 99.9;
    writeln(b.radius, ' ', a.radius);
end.
