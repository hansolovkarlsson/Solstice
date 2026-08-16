program TestPropertyBadSetterValueType;
type
    TCircle = class
    private
        FRadius: real;
        procedure SetRadius(r: real);
    public
        property Radius: real read FRadius write SetRadius;
    end;
var
    c: TCircle;

procedure TCircle.SetRadius;
begin
    FRadius := r;
end;

begin
    new(c);
    c.Radius := 'hello';
    dispose(c);
end.
