program TestRecnestWholeassign;
{ Expected output:
  before copy: r2 = (0, 0) (0, 0)
  after copy: r2 = (1, 2) (3, 4)
  r1 unaffected after mutating r2: (1, 2) (3, 4)
}
type
    TPoint = record
        x, y: integer;
    end;
    TRect = record
        topleft, bottomright: TPoint;
    end;
var
    r1, r2: TRect;
begin
    r1.topleft.x := 1;
    r1.topleft.y := 2;
    r1.bottomright.x := 3;
    r1.bottomright.y := 4;

    writeln('before copy: r2 = (', r2.topleft.x, ', ', r2.topleft.y, ') (',
            r2.bottomright.x, ', ', r2.bottomright.y, ')');

    r2 := r1;
    writeln('after copy: r2 = (', r2.topleft.x, ', ', r2.topleft.y, ') (',
            r2.bottomright.x, ', ', r2.bottomright.y, ')');

    r2.topleft.x := 99;
    writeln('r1 unaffected after mutating r2: (', r1.topleft.x, ', ', r1.topleft.y, ') (',
            r1.bottomright.x, ', ', r1.bottomright.y, ')');
end.
