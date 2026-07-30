program AuditI;
function build(n: integer): string;
var i: integer; s: string;
begin
    s := '.....';
    for i := 1 to n do
        s[i] := 'X';
    if n < 3 then
        writeln('n=', n, ': ', s, ' recursing...')
    else
        writeln('n=', n, ': ', s);
    if n < 3 then
        build(n + 1);
    build := s;
end;
begin
    build(1);
end.
