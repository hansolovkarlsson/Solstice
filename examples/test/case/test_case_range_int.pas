program TestCaseRangeInt;
var
    x: integer;
begin
    { a range label ('low..high') mixed freely with plain values, in the
      same arm and across different arms }
    for x := 0 to 10 do begin
        case x of
            1, 2, 5..8: writeln('range or single: ', x);
            3..4: writeln('mid: ', x);
            0: writeln('zero');
        else
            writeln('other: ', x);
        end;
    end;
end.
