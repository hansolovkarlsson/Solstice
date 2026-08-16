program TestAbstractPropertyGetter;
type
    TShape = class
    public
        function GetArea: real; abstract;
        property Area: real read GetArea;
    end;
    TCircle = class(TShape)
    private
        FRadius: real;
    public
        function GetArea: real;
        procedure SetRadius(r: real);
    end;
var
    c: TCircle;
    shape: TShape;

function TCircle.GetArea;
begin
    GetArea := 3.14159 * FRadius * FRadius;
end;

procedure TCircle.SetRadius;
begin
    FRadius := r;
end;

begin
    new(c);
    c.SetRadius(4.0);
    shape := c;
    writeln('Area = ', shape.Area:0:2);
end.
