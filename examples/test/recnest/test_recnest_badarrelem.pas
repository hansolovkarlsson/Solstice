program TestRecnestBadarrelem;
{ Expected: Compile Error - TRect (has nested field 'topleft') can't be an array element type }
type
    TPoint = record x, y: integer; end;
    TRect = record topleft, bottomright: TPoint; end;
var
    arr: array[1..2] of TRect;
begin
end.
