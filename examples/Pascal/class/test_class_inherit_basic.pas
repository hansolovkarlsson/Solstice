program TestClassInheritBasic;
type
    TShape = class
        name: integer;
        function Area: real;
    end;
    TCircle = class(TShape)
        radius: real;
        function Area: real;
    end;
var
    c: TCircle;

function TShape.Area;
begin
    Area := 0.0;
end;

function TCircle.Area;
begin
    Area := 3.14159 * self.radius * self.radius;
end;

begin
    new(c);
    { an inherited field, still accessible on the subclass }
    c.name := 1;
    c.radius := 2.0;
    writeln('name: ', c.name);
    { an OVERRIDDEN method - TCircle's own body wins }
    writeln('area: ', c.Area);
    dispose(c);
end.
