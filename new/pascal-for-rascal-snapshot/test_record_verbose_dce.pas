program TestRecordVerboseDce;
type TPoint = record x, y: integer; end;
var used, dead: TPoint;
begin
    used.x := 5;
    used.y := 10;
    dead.x := 999;
    dead.y := 888;
    writeln(used.x + used.y);
end.
