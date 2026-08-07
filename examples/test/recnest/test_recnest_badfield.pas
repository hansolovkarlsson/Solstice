program TestRecnestBadfield;
{ Expected: Compile Error - 'z' is not a field of TPoint (via 'topleft') }
type
    TPoint = record x, y: integer; end;
    TRect = record topleft, bottomright: TPoint; end;
var
    r: TRect;
begin
    r.topleft.z := 1;
end.
