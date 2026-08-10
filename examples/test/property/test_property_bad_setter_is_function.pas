program TestPropertyBadSetterIsFunction;
type
    TCircle = class
    private
        FRadius: real;
        function DoubleIt(r: real): real;
    public
        property Radius: real read FRadius write DoubleIt;
    end;
var
    c: TCircle;

function TCircle.DoubleIt;
begin
    DoubleIt := r * 2;
end;

begin
    new(c);
    dispose(c);
end.
