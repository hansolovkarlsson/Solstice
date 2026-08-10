program TestPropertyBadSetterWrongArity;
type
    TCircle = class
    private
        FRadius: real;
        procedure SetBounds(lo, hi: real);
    public
        property Radius: real read FRadius write SetBounds;
    end;
var
    c: TCircle;

procedure TCircle.SetBounds;
begin
    FRadius := lo + hi;
end;

begin
    new(c);
    dispose(c);
end.
