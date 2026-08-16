program TestAbstractBadBodyGiven;
type
    TShape = class
    public
        function Area: real; abstract;
    end;

function TShape.Area;
begin
    Area := 0.0;
end;

begin
end.
