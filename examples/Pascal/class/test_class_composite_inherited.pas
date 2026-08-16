program TestClassCompositeInherited;
type
    TPoint = record
        x, y: integer;
    end;
    TShape = class
        center: TPoint;
        function CenterSum: integer;
    end;
    TCircle = class(TShape)
        { never redeclares 'center' or 'CenterSum' - both purely
          inherited. The SUBCLASS's own flattened fields[] must still
          compute the correct offset for 'center', freshly, at TCircle's
          own declaration time - not reuse some offset cached from
          TShape. }
        radius: real;
    end;
var
    c: TCircle;

function TShape.CenterSum;
begin
    CenterSum := center.x + center.y;
end;

begin
    new(c);
    c.center.x := 3;
    c.center.y := 4;
    c.radius := 1.5;

    writeln('center: ', c.center.x, ', ', c.center.y);
    writeln('sum via inherited method: ', c.CenterSum);
    writeln('radius: ', c.radius:0:5);

    dispose(c);
end.
