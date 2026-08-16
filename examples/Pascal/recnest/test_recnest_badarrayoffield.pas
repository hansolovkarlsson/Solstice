program TestRecnestBadarrayoffield;
{ Expected: Compile Error - 'array of TPoint' isn't supported as a
  record field type (a distinct restriction from nesting a plain TPoint
  field) }
type
    TPoint = record x, y: integer; end;
    TPoly = record
        pts: array[1..3] of TPoint;
    end;
var
    p: TPoly;
begin
end.
