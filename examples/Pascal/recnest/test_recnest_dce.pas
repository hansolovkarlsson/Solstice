program TestRecnestDce;
type
    TPoint = record x, y: integer; end;
    TRect = record topleft, bottomright: TPoint; end;
var
    used, dead: TRect;
begin
    used.topleft.x := 5;
    used.topleft.y := 10;
    used.bottomright.x := 1;
    used.bottomright.y := 2;
    dead.topleft.x := 999;
    dead.topleft.y := 888;
    dead.bottomright.x := 777;
    dead.bottomright.y := 666;
    writeln(used.topleft.x + used.topleft.y + used.bottomright.x + used.bottomright.y);
end.
