program TestClassBadMethBodyUnkClass;
type
    TCircle = class
        radius: real;
    end;
var
    c: TCircle;

procedure Bogus.Foo;
begin
end;

{ Expected: Compile Error - 'Bogus' is not a declared class }
begin
end.
