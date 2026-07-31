program TestAliasBasic;
type
    TAge = integer;
    TYears = TAge;
    TName = string;
    TFraction = real;
    TFlag = boolean;
    TLetter = char;
var
    age: TAge;
    years: TYears;
    name: TName;
    ratio: TFraction;
    flag: TFlag;
    letter: TLetter;
    scores: array[1..3] of TAge;
    i: integer;
begin
    age := 30;
    years := age + 5;
    name := 'Ada';
    ratio := 3.5;
    flag := true;
    letter := 'Z';
    writeln('age = ', age);
    writeln('years = ', years);
    writeln('name = ', name);
    writeln('ratio = ', ratio:0:2);
    writeln('flag = ', flag);
    writeln('letter = ', letter);

    for i := 1 to 3 do
        scores[i] := i * 10;
    writeln('scores[2] = ', scores[2]);
end.
