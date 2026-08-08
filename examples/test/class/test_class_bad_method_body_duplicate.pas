program TestClassBadMethBodyDup;
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

procedure TCircle.SetRadius;
begin
    self.radius := r;
end;

{ Expected: Compile Error - 'TCircle.SetRadius' already has a body }
begin
end.
