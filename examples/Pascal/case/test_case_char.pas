program TestCaseChar;
var
    c: char;
begin
    c := 'b';
    case c of
        'a': writeln('A');
        'b', 'c': writeln('B or C');
    else
        writeln('other');
    end;

    c := #65; { 'A' by char code }
    case c of
        'A': writeln('char-code A matched');
    else
        writeln('no match');
    end;
end.
