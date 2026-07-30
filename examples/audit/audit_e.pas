program AuditE;
type TCircle = record radius: real; end;
var c: TCircle;
begin
    c.radius := 3.0;
    writeln('area: ', pi * sqr(c.radius):0:2);
    writeln('sqrt: ', sqrt(c.radius):0:4);
    writeln('power: ', power(c.radius, 2):0:2);
    c.radius := c.radius + 1.5;
    writeln('new radius: ', c.radius);
end.
