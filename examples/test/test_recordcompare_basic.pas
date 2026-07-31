program TestRecordCompareBasic;
type
    TPoint = record x, y: integer; end;
var
    p1, p2, p3: TPoint;
begin
    p1.x := 1; p1.y := 2;
    p2.x := 1; p2.y := 2;
    p3.x := 1; p3.y := 3;
    writeln('p1 = p2? ', p1 = p2);
    writeln('p1 = p3? ', p1 = p3);
    writeln('p1 <> p3? ', p1 <> p3);
    writeln('p1 <> p2? ', p1 <> p2);
end.
