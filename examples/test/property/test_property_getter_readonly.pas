program TestPropertyGetterReadonly;
type
    TCircle = class
    private
        FRadius: real;
        procedure SetRadius(r: real);
    public
        function GetArea: real;
        property Radius: real read FRadius write SetRadius;
        property Area: real read GetArea;
    end;
var
    c: TCircle;

procedure TCircle.SetRadius;
begin
    FRadius := r;
end;

function TCircle.GetArea;
begin
    GetArea := 3.14159 * FRadius * FRadius;
end;

begin
    new(c);
    c.Radius := 5.0;
    writeln('Area = ', c.Area:0:2);
    dispose(c);
end.
