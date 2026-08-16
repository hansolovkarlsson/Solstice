program TestClassBadInheritOverrideSig;
type
    TShape = class
        function Area: real;
    end;
    TCircle = class(TShape)
        radius: real;
        function Area: integer;
    end;
{ Expected: Compile Error - override signature must match exactly }
var
    c: TCircle;
begin
end.
