program TestRecnestBadtypemismatch;
{ Expected: Compile Error - TRect and TBox both nest TPoint but are
  structurally different, distinct record types - not interchangeable }
type
    TPoint = record x, y: integer; end;
    TRect = record topleft, bottomright: TPoint; end;
    TBox = record topleft, bottomright: TPoint; end;
var
    r: TRect;
    b: TBox;
begin
    r := b;
end.
