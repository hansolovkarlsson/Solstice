program TestPropertyBadDuplicateMethod;
type
    TCircle = class
    private
        FRadius: real;
    public
        function Radius: real;
        property Radius: real read FRadius;
    end;
var
    c: TCircle;

function TCircle.Radius;
begin
    Radius := FRadius;
end;

begin
    new(c);
    dispose(c);
end.
