program TestClassCompositeDeep;
type
    TPoint = record
        x, y: integer;
    end;
    TRect = record
        { a nested record whose own field is itself another nested
          record - exercises the multi-hop chain resolver, not just one
          level of '.' }
        topleft: TPoint;
        w, h: integer;
    end;
    TWidget = class
        bounds: TRect;
        id: integer;
    end;
var
    w: TWidget;
begin
    new(w);
    w.bounds.topleft.x := 5;
    w.bounds.topleft.y := 6;
    w.bounds.w := 100;
    w.bounds.h := 50;
    w.id := 7;

    writeln('topleft: ', w.bounds.topleft.x, ', ', w.bounds.topleft.y);
    writeln('size: ', w.bounds.w, ', ', w.bounds.h);
    writeln('id: ', w.id);

    dispose(w);
end.
