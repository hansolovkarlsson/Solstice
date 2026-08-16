program TestClassFieldAccess;
type
    TCircle = class
        radius: real;
        count: integer;
    end;
var
    c: TCircle;
begin
    new(c);
    c.radius := 2.5;
    c.count := 3;
    writeln('radius: ', c.radius);
    writeln('count: ', c.count);
    c.count := c.count + 1;
    writeln('count after increment: ', c.count);
    dispose(c);
end.
