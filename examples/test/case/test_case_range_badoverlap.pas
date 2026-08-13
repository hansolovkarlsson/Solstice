program TestCaseRangeBadOverlap;
var
    x: integer;
begin
    case x of
        1..5: writeln('a');
        3..7: writeln('b');
    end;
end.
