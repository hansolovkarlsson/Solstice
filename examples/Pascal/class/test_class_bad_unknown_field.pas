program TestClassBadUnknownField;
type
    TCircle = class
        radius: real;
    end;
{ Expected: Compile Error - 'diameter' is not a field of the class's
  hidden backing record }
var
    c: TCircle;
begin
    new(c);
    c.diameter := 1.0;
end.
