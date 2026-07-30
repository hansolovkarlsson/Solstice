program TestRecordBadField;
type TPoint = record x, y: integer; end;
var p: TPoint;
begin
    p.z := 5;
end.
