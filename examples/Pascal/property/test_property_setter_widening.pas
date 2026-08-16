program TestPropertySetterWidening;
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
    { integer literal assigned to a 'real' property backed by a setter -
      confirms build_property_setter_call()'s int->real widening }
    c.Radius := 2;
    writeln('Radius = ', c.Radius:0:2);
    dispose(c);
end.
