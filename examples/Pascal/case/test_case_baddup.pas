program TestCaseBadDup;
var
    x: integer;
begin
    case x of
        1: writeln('a');
        2, 1: writeln('b');
    end;
end.
