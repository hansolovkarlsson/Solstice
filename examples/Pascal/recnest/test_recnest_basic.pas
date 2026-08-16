program TestRecnestBasic;
{ Expected output:
  r.topleft = (0, 0)
  r.bottomright = (3, 4)
  after translate: (1, 1) (4, 5)
}
type
    TPoint = record
        x, y: integer;
    end;
    TRect = record
        topleft, bottomright: TPoint;
    end;
var
    r: TRect;
begin
    r.topleft.x := 0;
    r.topleft.y := 0;
    r.bottomright.x := 3;
    r.bottomright.y := 4;
    writeln('r.topleft = (', r.topleft.x, ', ', r.topleft.y, ')');
    writeln('r.bottomright = (', r.bottomright.x, ', ', r.bottomright.y, ')');

    r.topleft.x := r.topleft.x + 1;
    r.topleft.y := r.topleft.y + 1;
    r.bottomright.x := r.bottomright.x + 1;
    r.bottomright.y := r.bottomright.y + 1;
    writeln('after translate: (', r.topleft.x, ', ', r.topleft.y, ') (',
            r.bottomright.x, ', ', r.bottomright.y, ')');
end.
