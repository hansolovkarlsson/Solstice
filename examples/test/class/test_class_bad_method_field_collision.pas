program TestClassBadFieldMethodClash;
type
    TCircle = class
        radius: real;
        function radius: real;
    end;
{ Expected: Compile Error - 'radius' is already a field of class TCircle }
var
    c: TCircle;
begin
end.
