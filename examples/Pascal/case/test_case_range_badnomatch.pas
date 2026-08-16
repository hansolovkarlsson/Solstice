program TestCaseRangeBadNoMatch;
var
    x: integer;
begin
    { a range doesn't widen to catch everything - a value outside every
      range, with no else clause, must still be a runtime error }
    x := 20;
    case x of
        1..5: writeln('a');
    end;
    writeln('unreachable');
end.
