program TestCaseRangeChar;
var
    c: char;
begin
    c := 'a';
    case c of
        'a'..'z': writeln('lower');
        'A'..'Z': writeln('upper');
    else
        writeln('other');
    end;

    c := 'Z';
    case c of
        'a'..'z': writeln('lower');
        'A'..'Z': writeln('upper');
    else
        writeln('other');
    end;

    c := '5';
    case c of
        'a'..'z': writeln('lower');
        'A'..'Z': writeln('upper');
    else
        writeln('other');
    end;
end.
