program TestLocalForBreakContinue;

function sumSkipEven(n: integer): integer;
var i, s: integer;
begin
    s := 0;
    for i := 1 to n do begin
        if i mod 2 = 0 then continue;
        if i > 7 then break;
        s := s + i;
    end;
    sumSkipEven := s;
end;

begin
    writeln('sumSkipEven(10) = ', sumSkipEven(10));  { 1+3+5+7 = 16 }
end.
