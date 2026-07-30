program DceArray;
var
    dead: array[1..5] of integer;
    i: integer;
begin
    for i := 1 to 5 do
        dead[i] := i * 100 div (i - i + 1);  { deliberately complex RHS to prove it's evaluated/freed, not just skipped }
    writeln('done');
end.
