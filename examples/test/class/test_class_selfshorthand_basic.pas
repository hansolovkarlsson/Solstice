program TestClassSelfshorthandBasic;
type
    TCircle = class
        radius: real;
        function Area: real;
        procedure SetRadius(r: real);
        procedure DoubleRadius;
    end;
var
    c: TCircle;

procedure TCircle.SetRadius;
begin
    { bare 'radius' as an assignment target - no 'self.' }
    radius := r;
end;

procedure TCircle.DoubleRadius;
begin
    { bare 'radius' read AND written in the same statement }
    radius := radius * 2.0;
end;

function TCircle.Area;
begin
    { bare method call (no parens, matching the existing parenless-call
      rule for a zero-argument method), then a bare field read }
    DoubleRadius;
    Area := 3.14159 * radius * radius;
end;

begin
    new(c);

    c.SetRadius(2.0);
    writeln('radius after SetRadius: ', c.radius:0:5);

    { Area's own body doubles radius via shorthand before computing -
      confirms the shorthand call actually mutated the field through
      'self', not some detached copy. }
    writeln('area: ', c.Area:0:5);
    writeln('radius after Area: ', c.radius:0:5);

    dispose(c);
end.
