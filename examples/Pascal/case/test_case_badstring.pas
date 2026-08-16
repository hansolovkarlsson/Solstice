program TestCaseBadString;
var
    s: string;
begin
    s := 'x';
    case s of
        'a': writeln('a');
    end;
end.
