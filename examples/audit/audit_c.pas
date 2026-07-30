program AuditC;
type TStudent = record scores: array[1..5] of integer; end;
var s: TStudent;
begin
    writeln(low(s.scores));
    writeln(high(s.scores));
    writeln(length(s.scores));
end.
