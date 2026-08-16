program TestClassBadDuplicateMethod;
type
    TCircle = class
        radius: real;
        procedure SetRadius(r: real);
        procedure SetRadius(r: real);
    end;
{ Expected: Compile Error - duplicate method 'SetRadius' in class TCircle }
var
    c: TCircle;
begin
end.
