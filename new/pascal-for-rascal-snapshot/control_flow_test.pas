program ControlFlowTest;
var
    i, n, sum: integer;
    isEven: boolean;

begin
    { --- if / then / else --- }
    n := 7;
    if n > 5 then
        writeln(1)
    else
        writeln(0);

    if n > 100 then
        writeln(999);   { should not print: condition is false, no else }

    { --- while / do, with a nested if / else inside the loop --- }
    i := 1;
    sum := 0;
    while i <= 10 do begin
        sum := sum + i;
        isEven := i mod 2 = 0;
        if isEven then
            writeln(i)
        else begin
            writeln(-i);
        end;
        i := i + 1;
    end;
    writeln(sum);   { expect 55 }

    { --- repeat / until --- }
    n := 0;
    repeat
        n := n + 1;
        writeln(n * n);
    until n = 5;

    { --- nested while inside if --- }
    if sum > 50 then begin
        i := 3;
        while i > 0 do begin
            writeln(i);
            i := i - 1;
        end;
    end else begin
        writeln(0);
    end;
end.
