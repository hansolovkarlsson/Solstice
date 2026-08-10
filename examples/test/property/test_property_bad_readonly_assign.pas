program TestPropertyBadReadonlyAssign;
type
    TCircle = class
    private
        FRadius: real;
    public
        function GetArea: real;
        property Area: real read GetArea;
    end;
var
    c: TCircle;

function TCircle.GetArea;
begin
    GetArea := 3.14159 * FRadius * FRadius;
end;

begin
    new(c);
    c.Area := 5.0;
    dispose(c);
end.
