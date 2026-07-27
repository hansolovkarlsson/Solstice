program ForTest;
var
    i, j, sum: integer;
begin
    for i := 1 to 5 do
        writeln(i);

    writeln('---');
    for i := 5 downto 1 do
        writeln(i);

    writeln('---');
    sum := 0;
    for i := 1 to 10 do
        sum := sum + i;
    writeln(sum);

    writeln('---');
    for i := 1 to 3 do
        for j := 1 to 3 do
            writeln(i * 10 + j);

    writeln('---');
    for i := 5 to 1 do
        writeln('should not print');
    writeln('empty range ok');
end.
