program TestRecnestParam;
{ Expected output:
  Area(g) = 6
  local test: (0, 0) (10, 10)
  g still: (0, 0) (2, 3)
}
type
    TPoint = record
        x, y: integer;
    end;
    TRect = record
        topleft, bottomright: TPoint;
    end;

var
    g: TRect;

function Area(r: TRect): integer;
begin
    Area := (r.bottomright.x - r.topleft.x) * (r.bottomright.y - r.topleft.y);
end;

procedure LocalTest;
var
    l: TRect;
begin
    l.topleft.x := 0;
    l.topleft.y := 0;
    l.bottomright.x := 10;
    l.bottomright.y := 10;
    writeln('local test: (', l.topleft.x, ', ', l.topleft.y, ') (',
            l.bottomright.x, ', ', l.bottomright.y, ')');
end;

begin
    g.topleft.x := 0;
    g.topleft.y := 0;
    g.bottomright.x := 2;
    g.bottomright.y := 3;
    writeln('Area(g) = ', Area(g));

    LocalTest;

    writeln('g still: (', g.topleft.x, ', ', g.topleft.y, ') (',
            g.bottomright.x, ', ', g.bottomright.y, ')');
end.
