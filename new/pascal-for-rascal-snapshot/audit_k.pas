program AuditK;
type TBucket = record vals: array[1..3] of integer; end;
var b: TBucket;

function fillAndSum(n: integer): integer;
var i, s: integer;
begin
    for i := 1 to 3 do
        b.vals[i] := b.vals[i] + n;
    s := 0;
    for i := 1 to 3 do
        s := s + b.vals[i];
    if n < 3 then
        fillAndSum(n + 1);
    fillAndSum := s;
end;

begin
    writeln(fillAndSum(1));
    writeln('final: ', b.vals[1], ' ', b.vals[2], ' ', b.vals[3]);
end.
