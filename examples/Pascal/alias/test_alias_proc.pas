program TestAliasProc;
type
    TAge = integer;
    TPoint = record
        x, y: TAge;
    end;
var
    p: TPoint;

function nextAge(a: TAge): TAge;
var
    temp: TAge;
begin
    temp := a + 1;
    nextAge := temp;
end;

begin
    writeln('nextAge(5) = ', nextAge(5));
    p.x := 3;
    p.y := 4;
    writeln('p = (', p.x, ', ', p.y, ')');
end.
