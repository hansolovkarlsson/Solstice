program TestRecnestBaddrill;
{ Expected: Compile Error - 'r.topleft' names a whole record, needs a further field }
type
    TPoint = record x, y: integer; end;
    TRect = record topleft, bottomright: TPoint; end;
var
    r: TRect;
    n: integer;
begin
    n := r.topleft;
end.
