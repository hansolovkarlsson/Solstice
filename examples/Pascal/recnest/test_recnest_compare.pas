program TestRecnestCompare;
{ Expected output:
  r1 = r2? TRUE
  r1 = r3? FALSE
  r1 <> r3? TRUE
  r1 <> r2? FALSE
}
type
    TPoint = record
        x, y: integer;
    end;
    TRect = record
        topleft, bottomright: TPoint;
    end;
var
    r1, r2, r3: TRect;
begin
    r1.topleft.x := 1; r1.topleft.y := 2;
    r1.bottomright.x := 3; r1.bottomright.y := 4;

    r2.topleft.x := 1; r2.topleft.y := 2;
    r2.bottomright.x := 3; r2.bottomright.y := 4;

    r3.topleft.x := 1; r3.topleft.y := 2;
    r3.bottomright.x := 3; r3.bottomright.y := 99;

    writeln('r1 = r2? ', r1 = r2);
    writeln('r1 = r3? ', r1 = r3);
    writeln('r1 <> r3? ', r1 <> r3);
    writeln('r1 <> r2? ', r1 <> r2);
end.
