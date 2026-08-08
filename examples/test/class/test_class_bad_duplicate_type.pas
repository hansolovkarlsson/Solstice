program TestClassBadDuplicateType;
type
    TCircle = class
        radius: real;
    end;
    TCircle = class
        side: real;
    end;
{ Expected: Compile Error - duplicate type declaration 'TCircle' }
var
    c: TCircle;
begin
end.
