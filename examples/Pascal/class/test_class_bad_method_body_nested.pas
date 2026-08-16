program TestClassBadMethBodyNested;
type
    TCircle = class
        radius: real;
        procedure SetRadius(r: real);
    end;
var
    c: TCircle;

procedure Outer;
    procedure TCircle.SetRadius;
    begin
        self.radius := r;
    end;
begin
end;

{ Expected: Compile Error - a class method body must be declared at the
  top level, not nested inside another procedure }
begin
end.
