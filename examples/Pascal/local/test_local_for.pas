program TestLocalFor;
var total: integer;

function sumTo(n: integer): integer;
var i, s: integer;
begin
    s := 0;
    for i := 1 to n do
        s := s + i;
    sumTo := s;
end;

function sumRange(lo, hi: integer): integer;
var i, s: integer;
begin
    s := 0;
    for i := lo to hi do
        s := s + i;
    sumRange := s;
end;

function countdown(n: integer): integer;
var i, count: integer;
begin
    count := 0;
    for i := n downto 1 do
        count := count + 1;
    countdown := count;
end;

begin
    total := sumTo(10);
    writeln('sumTo(10) = ', total);        { 55 }
    writeln('sumRange(5,8) = ', sumRange(5, 8)); { 26 }
    writeln('countdown(5) = ', countdown(5));    { 5 }
end.
