program TestWithBadMultiSecond;
type
    TPoint = record n: integer; end;
var
    a: TPoint;
begin
    with a, nothere do begin
        n := 1;
    end;
end.
