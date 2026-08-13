program TestCaseRangeEnum;
type
    TColor = (Red, Green, Blue, Yellow);
var
    col: TColor;
begin
    col := Green;
    case col of
        Red..Blue: writeln('warm-ish range');
        Yellow: writeln('yellow');
    end;

    col := Yellow;
    case col of
        Red..Blue: writeln('warm-ish range');
        Yellow: writeln('yellow');
    end;
end.
