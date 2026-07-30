program AuditC2;
type TStudent = record scores: array[1..5] of integer; end;
var s: TStudent; i: integer;
begin
    for i := low(s.scores) to high(s.scores) do
        s.scores[i] := i * i;
    writeln('length: ', length(s.scores));
    for i := low(s.scores) to high(s.scores) do
        write(s.scores[i], ' ');
    writeln;
end.
