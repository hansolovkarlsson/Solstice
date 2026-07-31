program TestSubrangeBadReturn;
type
    TAge = 0..150;

function nextAge(a: TAge): TAge;
begin
    nextAge := a + 1;
end;

begin
    writeln('nextAge(150) = ', nextAge(150));
end.
