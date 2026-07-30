program DocRecordExample;
type
    TPoint = record
        x, y: integer;
    end;
var
    origin, p: TPoint;
begin
    origin.x := 0;
    origin.y := 0;

    p.x := 3;
    p.y := 4;
    writeln('p = (', p.x, ', ', p.y, ')');

    p := origin;
    writeln('p after p := origin: (', p.x, ', ', p.y, ')');
end.
