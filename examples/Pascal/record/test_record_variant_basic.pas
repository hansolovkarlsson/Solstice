program TestRecordVariantBasic;
type
    TShape = record
        case kind: integer of
            0: (radius: real);
            1: (width, height: real);
    end;
var
    s: TShape;
begin
    s.kind := 0;
    s.radius := 2.5;
    writeln('kind: ', s.kind);
    writeln('radius: ', s.radius);

    s.kind := 1;
    s.width := 3.0;
    s.height := 4.0;
    writeln('kind: ', s.kind);
    writeln('width: ', s.width);
    writeln('height: ', s.height);
end.
