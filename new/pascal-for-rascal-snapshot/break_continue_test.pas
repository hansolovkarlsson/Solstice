program BreakContinueTest;
var
    i, j: integer;
begin
    { break in while }
    i := 0;
    while true do begin
        i := i + 1;
        if i = 5 then break;
    end;
    writeln('while break at i=', i);

    { continue in while }
    i := 0;
    j := 0;
    while i < 10 do begin
        i := i + 1;
        if i mod 2 = 0 then continue;
        j := j + i;
    end;
    writeln('sum of odds 1..10 = ', j);

    { break in for }
    for i := 1 to 100 do begin
        if i = 7 then break;
        writeln('for-break: ', i);
    end;

    { continue in for }
    for i := 1 to 6 do begin
        if i mod 2 = 0 then continue;
        writeln('for-continue odd: ', i);
    end;

    { break in repeat }
    i := 0;
    repeat
        i := i + 1;
        if i = 3 then break;
    until false;
    writeln('repeat-break at i=', i);

    { continue in repeat }
    i := 0;
    j := 0;
    repeat
        i := i + 1;
        if i mod 2 = 0 then continue;
        j := j + 1;
    until i >= 6;
    writeln('repeat-continue count of odds = ', j);

    { nested loops: break only exits innermost }
    for i := 1 to 3 do begin
        for j := 1 to 3 do begin
            if j = 2 then break;
            writeln('nested ', i, ' ', j);
        end;
    end;
end.
