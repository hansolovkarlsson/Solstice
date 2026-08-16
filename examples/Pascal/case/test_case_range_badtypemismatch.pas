program TestCaseRangeBadTypeMismatch;
var
    x: integer;
begin
    case x of
        1..'z': writeln('a');
    end;
end.
