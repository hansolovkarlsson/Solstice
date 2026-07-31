program TestCaseBadTypeMismatch;
var
    x: integer;
begin
    case x of
        1: writeln('a');
        'b': writeln('b');
    end;
end.
