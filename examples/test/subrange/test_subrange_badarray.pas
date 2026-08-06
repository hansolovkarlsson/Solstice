program TestSubrangeBadArray;
type
    TAge = 0..150;
var
    ages: array[1..3] of TAge;
begin
    ages[1] := 999;
end.
