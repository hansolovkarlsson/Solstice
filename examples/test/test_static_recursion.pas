program TestStaticRecursion;
type
    TAge = 0..150;

function countUp(n: integer): integer;
var
    static calls: integer;
begin
    inc(calls);
    if n <= 0 then
        countUp := calls
    else
        countUp := countUp(n - 1);
end;

procedure bump;
var
    static calls: integer;
    static a: TAge;
begin
    inc(calls);
    a := calls;
    writeln('bump calls = ', calls, ' a = ', a);
end;

begin
    writeln('countUp(3) total calls = ', countUp(3));
    bump;
    bump;
    bump;
end.
