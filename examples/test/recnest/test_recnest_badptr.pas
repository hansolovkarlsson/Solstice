program TestRecnestBadptr;
{ Expected: Compile Error - TRect (has nested field 'topleft') can't be a pointer target type }
type
    TPoint = record x, y: integer; end;
    TRect = record topleft, bottomright: TPoint; end;
    PRect = ^TRect;
var
    p: PRect;
begin
end.
