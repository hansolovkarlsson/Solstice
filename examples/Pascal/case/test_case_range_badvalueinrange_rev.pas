program TestCaseRangeBadValInRangeRev;
var
    x: integer;
begin
    { same overlap as test_case_range_badvalueinrange.pas, but the plain
      value is declared BEFORE the range that contains it }
    case x of
        3: writeln('b');
        1..5: writeln('a');
    end;
end.
