program TestClassCompositeMulti;
type
    TPoint = record
        x, y: integer;
    end;
    TLine = class
        { two SEPARATE nested-record fields - the second one's base
          offset must correctly account for the first field's full leaf
          count (2 slots), not just assume every field is 1 slot. }
        start: TPoint;
        finish: TPoint;
        tag: integer;
    end;
var
    l: TLine;
begin
    new(l);
    l.start.x := 1;
    l.start.y := 2;
    l.finish.x := 10;
    l.finish.y := 20;
    l.tag := 42;

    writeln('start: ', l.start.x, ', ', l.start.y);
    writeln('finish: ', l.finish.x, ', ', l.finish.y);
    writeln('tag: ', l.tag);

    { mutating 'finish' must not disturb 'start' or 'tag' }
    l.finish.x := 999;
    writeln('start unchanged: ', l.start.x, ', ', l.start.y);
    writeln('tag unchanged: ', l.tag);

    dispose(l);
end.
