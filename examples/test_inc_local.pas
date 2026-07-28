program TestIncLocal;

function countUp(n: integer): integer;
var total: integer;
begin
    total := 0;
    while n > 0 do begin
        inc(total, n);
        dec(n);
    end;
    countUp := total;
end;

begin
    writeln(countUp(5));   { 5+4+3+2+1 = 15 }
end.
