program TestRecordTypeMismatch;
type
    TPoint = record x, y: integer; end;
    TColor = record r, g, b: integer; end;
var p: TPoint; c: TColor;
begin
    p := c;
end.
