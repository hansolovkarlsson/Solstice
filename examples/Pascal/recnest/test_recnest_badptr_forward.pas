program TestRecnestBadptrForward;
{ Expected: Compile Error - forward-declared pointer target TRect has a
  nested field, caught by the pending-pointer-target resolution pass }
type
    PRect = ^TRect;
    TPoint = record x, y: integer; end;
    TRect = record topleft, bottomright: TPoint; end;
var
    p: PRect;
begin
end.
