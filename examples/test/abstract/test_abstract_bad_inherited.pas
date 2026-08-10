program TestAbstractBadInherited;
type
    TShape = class
    public
        function Area: real; abstract;
    end;
    TCircle = class(TShape)
    public
        function Area: real;
    end;

function TCircle.Area;
begin
    Area := inherited Area;
end;

begin
end.
