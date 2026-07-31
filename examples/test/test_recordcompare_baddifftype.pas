program TestRecordCompareBadDiffType;
type
    TPoint = record x, y: integer; end;
    TSize = record w, h: integer; end;
var
    p: TPoint;
    s: TSize;
begin
    if p = s then writeln('same');
end.
