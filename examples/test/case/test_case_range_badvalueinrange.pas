program TestCaseRangeBadValInRange;
var
    x: integer;
begin
    case x of
        1..5: writeln('a');
        3: writeln('b');
    end;
end.
