program TestIsasAncestorVar;
type
    TShape = class
    public
        function Area: real;
    end;
    TCircle = class(TShape)
    private
        FRadius: real;
    public
        function Area: real;
        procedure SetRadius(r: real);
    end;
var
    shape: TShape;
    c: TCircle;

function TShape.Area;
begin
    Area := 0.0;
end;

function TCircle.Area;
begin
    Area := 3.14159 * FRadius * FRadius;
end;

procedure TCircle.SetRadius;
begin
    FRadius := r;
end;

begin
    new(c);
    c.SetRadius(5.0);
    { upcast: shape's STATIC type is TShape, but its runtime tag still
      says TCircle - this divergence is exactly what is/as test }
    shape := c;
    writeln('shape is TCircle: ', shape is TCircle);
    writeln('shape is TShape: ', shape is TShape);
    if shape is TCircle then begin
        c := shape as TCircle;
        writeln('casted area: ', c.Area:0:2);
    end;
    dispose(c);
end.
