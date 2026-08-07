program TestRecnestBadwith;
{ Expected: Compile Error - 'with' doesn't support a nested-record field }
type
    TPoint = record x, y: integer; end;
    TRect = record topleft, bottomright: TPoint; end;
var
    r: TRect;
begin
    with r do begin
        writeln(topleft.x);
    end;
end.
