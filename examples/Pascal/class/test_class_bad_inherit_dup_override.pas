program TestClassBadInheritDupOverride;
type
    TShape = class
        function Area: real;
    end;
    TCircle = class(TShape)
        radius: real;
        function Area: real;
        function Area: real;
    end;
{ Expected: Compile Error - duplicate method 'Area' in class 'TCircle' }
var
    c: TCircle;
begin
end.
