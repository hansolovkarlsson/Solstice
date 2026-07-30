program TestRecordMultiType;
type
    TPoint = record x, y: integer; end;
    TColor = record r, g, b: integer; end;
var
    p1, p2: TPoint;
    c: TColor;
begin
    p1.x := 1; p1.y := 2;
    p2.x := 10; p2.y := 20;
    c.r := 255; c.g := 0; c.b := 128;

    writeln('p1: ', p1.x, ',', p1.y);
    writeln('p2: ', p2.x, ',', p2.y);
    writeln('c: ', c.r, ',', c.g, ',', c.b);

    p2 := p1;
    writeln('p2 after copy: ', p2.x, ',', p2.y);
end.
