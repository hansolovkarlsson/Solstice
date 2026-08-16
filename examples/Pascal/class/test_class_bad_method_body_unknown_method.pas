program TestClassBadMethBodyUnkMeth;
type
    TCircle = class
        radius: real;
        procedure SetRadius(r: real);
    end;
var
    c: TCircle;

procedure TCircle.Bogus;
begin
end;

{ Expected: Compile Error - 'Bogus' is not a declared method of class 'TCircle' }
begin
end.
