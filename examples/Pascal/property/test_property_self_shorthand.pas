program TestPropertySelfShorthand;
type
    TCircle = class
    private
        FRadius: real;
        procedure SetRadius(r: real);
    public
        function GetArea: real;
        procedure Describe;
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

procedure TCircle.Describe;
begin
    Radius := 3.0;
    writeln('self-shorthand Radius = ', Radius:0:2);
    writeln('self-shorthand Area = ', Area:0:2);
end;

begin
    new(c);
    c.Describe;
    dispose(c);
end.
