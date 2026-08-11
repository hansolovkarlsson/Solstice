program TestForInStringBadTypeMismatch;
var
    s: string;
    x: integer;
begin
    s := 'abc';
    for x in s do
        writeln(x);
end.
