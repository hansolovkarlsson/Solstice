program TestRecordVariantEnum;
type
    TKind = (Circle, Rectangle);
    TShape = record
        case kind: TKind of
            Circle: (radius: real);
            Rectangle: (width, height: real);
    end;
var
    s: TShape;
begin
    s.kind := Circle;
    s.radius := 1.5;
    writeln('kind: ', s.kind);
    writeln('radius: ', s.radius);

    s.kind := Rectangle;
    s.width := 2.0;
    s.height := 5.0;
    writeln('kind: ', s.kind);
    writeln('width: ', s.width);
    writeln('height: ', s.height);
end.
