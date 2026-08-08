program TestClassCallBasic;
type
    TCircle = class
        radius: real;
        procedure SetRadius(r: real);
        function Area: real;
    end;
var
    c: TCircle;

procedure TCircle.SetRadius;
begin
    self.radius := r;
end;

function TCircle.Area;
begin
    Area := 3.14159 * self.radius * self.radius;
end;

begin
    new(c);
    c.SetRadius(2.0);
    writeln('radius: ', c.radius);
    writeln('area: ', c.Area);
    dispose(c);
end.
