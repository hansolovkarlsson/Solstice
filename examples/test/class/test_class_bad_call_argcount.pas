program TestClassBadCallArgcount;
type
    TCircle = class
        radius: real;
        procedure SetRadius(r: real);
    end;
var
    c: TCircle;

procedure TCircle.SetRadius;
begin
    self.radius := r;
end;

begin
    new(c);
    c.SetRadius(2.0, 3.0);
    dispose(c);
end.
