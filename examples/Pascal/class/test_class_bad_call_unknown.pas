program TestClassBadCallUnknown;
type
    TCircle = class
        radius: real;
    end;
var
    c: TCircle;
begin
    new(c);
    c.Bogus;
    dispose(c);
end.
