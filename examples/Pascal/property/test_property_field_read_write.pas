program TestPropertyFieldReadWrite;
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
    writeln('SetRadius called with ', r:0:2);
    FRadius := r;
end;

begin
    new(c);
    c.Radius := 5.0;
    writeln('Radius = ', c.Radius:0:2);
    dispose(c);
end.
