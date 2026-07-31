program TestCaseBadNoMatch;
var
    x: integer;
begin
    x := 99;
    case x of
        1: writeln('one');
        2: writeln('two');
    end;
    writeln('unreachable');
end.
