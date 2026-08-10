program TestIsasDescFails;
type
    TShape = class
    public
        x: integer;
    end;
    TCircle = class(TShape)
    public
        y: integer;
    end;
var
    shape: TShape;
    c: TCircle;
begin
    { shape's runtime tag is actually TShape - a plain TShape, never a
      TCircle - so 'is TCircle'/'as TCircle' must both fail }
    new(shape);
    writeln('shape is TCircle: ', shape is TCircle);
    try
        c := shape as TCircle;
        writeln('BUG: should not reach here');
    except
        writeln('Caught: ', ExceptMessage);
    end;
    dispose(shape);
end.
