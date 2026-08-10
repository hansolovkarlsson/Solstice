program TestPropertyBadGetterWrongArity;
type
    TCircle = class
    private
        FRadius: real;
    public
        function ScaledArea(factor: real): real;
        property Area: real read ScaledArea;
    end;
var
    c: TCircle;

function TCircle.ScaledArea;
begin
    ScaledArea := factor * FRadius * FRadius;
end;

begin
    new(c);
    dispose(c);
end.
