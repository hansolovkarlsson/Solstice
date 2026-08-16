program TestSubrangeScope;
const
    MaxAge = 150;
type
    TAge = 0..MaxAge;
    TYears = TAge;
    TPerson = record
        name: string;
        age: TAge;
    end;
var
    p: TPerson;
    ages: array[1..3] of TAge;
    y: TYears;
    i: integer;

function nextAge(a: TAge): TAge;
begin
    nextAge := a + 1;
end;

begin
    p.name := 'Ada';
    p.age := 36;
    writeln(p.name, ' is ', p.age);

    for i := 1 to 3 do
        ages[i] := i * 10;
    writeln('ages[2] = ', ages[2]);

    y := 99;
    writeln('y = ', y);

    writeln('nextAge(36) = ', nextAge(36));
end.
