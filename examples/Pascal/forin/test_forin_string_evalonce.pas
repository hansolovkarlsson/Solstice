program TestForInStringEvalOnce;
var
    calls: integer;
    c: char;

function BuildString: string;
begin
    calls := calls + 1;
    BuildString := 'hi';
end;

begin
    calls := 0;
    for c in BuildString do
        writeln(c);
    writeln('calls=', calls);
end.
