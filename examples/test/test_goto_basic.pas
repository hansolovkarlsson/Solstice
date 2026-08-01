program TestGotoBasic;
label 1, 2;
var
    i: integer;
begin
    i := 1;
    1: writeln(i);
    i := i + 1;
    if i <= 3 then
        goto 1;
    goto 2;
    writeln('unreachable');
    2: writeln('done');
end.
