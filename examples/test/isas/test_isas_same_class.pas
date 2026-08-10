program TestIsasSameClass;
type
    TCircle = class
    public
        radius: real;
    end;
var
    c: TCircle;
begin
    new(c);
    c.radius := 5.0;
    writeln('c is TCircle: ', c is TCircle);
    c := c as TCircle;
    writeln('radius after cast: ', c.radius:0:2);
    dispose(c);
end.
