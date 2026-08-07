program TestRecnestSamefield;
{ Expected output:
  r1: (1,2) (3,4)
  r2: (10,20) (30,40)
  line.from = (5, 6)
  line.to = (7, 8)
}
type
    TPoint = record
        x, y: integer;
    end;
    TRect = record
        topleft, bottomright: TPoint;
    end;
    TLine = record
        from, to_: TPoint;
    end;
var
    r1, r2: TRect;
    line: TLine;
begin
    r1.topleft.x := 1; r1.topleft.y := 2;
    r1.bottomright.x := 3; r1.bottomright.y := 4;
    r2.topleft.x := 10; r2.topleft.y := 20;
    r2.bottomright.x := 30; r2.bottomright.y := 40;
    writeln('r1: (', r1.topleft.x, ',', r1.topleft.y, ') (', r1.bottomright.x, ',', r1.bottomright.y, ')');
    writeln('r2: (', r2.topleft.x, ',', r2.topleft.y, ') (', r2.bottomright.x, ',', r2.bottomright.y, ')');

    line.from.x := 5; line.from.y := 6;
    line.to_.x := 7; line.to_.y := 8;
    writeln('line.from = (', line.from.x, ', ', line.from.y, ')');
    writeln('line.to = (', line.to_.x, ', ', line.to_.y, ')');
end.
