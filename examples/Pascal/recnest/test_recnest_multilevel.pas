program TestRecnestMultilevel;
{ Expected output:
  b.corner.topleft = (1, 2)
  b.corner.bottomright = (3, 4)
  b.label = 5
}
type
    TPoint = record
        x, y: integer;
    end;
    TRect = record
        topleft, bottomright: TPoint;
    end;
    TBox = record
        corner: TRect;
        tag: integer;
    end;
var
    b: TBox;
begin
    b.corner.topleft.x := 1;
    b.corner.topleft.y := 2;
    b.corner.bottomright.x := 3;
    b.corner.bottomright.y := 4;
    b.tag := 5;
    writeln('b.corner.topleft = (', b.corner.topleft.x, ', ', b.corner.topleft.y, ')');
    writeln('b.corner.bottomright = (', b.corner.bottomright.x, ', ', b.corner.bottomright.y, ')');
    writeln('b.label = ', b.tag);
end.
