program TestGlobalForReadlnRegression;
var i, total, x: integer;
begin
    total := 0;
    for i := 1 to 5 do
        total := total + i;
    writeln('global for total: ', total);

    readln(x);
    writeln('global readln x: ', x);
end.
