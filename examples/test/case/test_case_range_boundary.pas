program TestCaseRangeBoundary;
var
    x: integer;
begin
    { the low and high bounds themselves must match - off-by-one is the
      obvious failure mode for a range check }
    for x := 4 to 9 do begin
        case x of
            5..8: writeln(x, ' in range');
        else
            writeln(x, ' out of range');
        end;
    end;
end.
