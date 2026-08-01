program TestForInBreakContinue;
var
    s: set of 0..9;
    x: integer;
begin
    s := [1, 2, 3, 4, 5, 6, 7];
    for x in s do begin
        if x = 5 then break;
        if x mod 2 = 0 then continue;
        writeln(x);
    end;
end.
