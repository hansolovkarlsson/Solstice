program TestClassBadInheritBodyNoOver;
type
    TShape = class
        function Area: real;
    end;
    TCircle = class(TShape)
        radius: real;
    end;
var
    c: TCircle;

function TShape.Area;
begin
    Area := 0.0;
end;

{ TCircle never redeclared 'Area' in its own class body, so it can't be
  given a body here either - inherited, not overridden. Expected:
  Compile Error. }
function TCircle.Area;
begin
    Area := 3.14159 * self.radius * self.radius;
end;

begin
end.
