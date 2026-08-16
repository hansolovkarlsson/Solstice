program TestClassMethodBasic;
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
    { Step 4 only: no '.Method(...)' call sugar yet (that's step 5) -
      call the mangled procedure directly by its real registered name,
      passing 'self' explicitly. }
    new(c);
    TCircle__SetRadius(c, 2.0);
    writeln('radius: ', c.radius);
    writeln('area: ', TCircle__Area(c));
    dispose(c);
end.
