program TestRecordDupType;
type
    TPoint = record x: integer; end;
    TPoint = record y: integer; end;
var p: TPoint;
begin
    p.x := 1;
end.
