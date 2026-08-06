program TestSubrangeBadParam;
type
    TAge = 0..150;

procedure showAge(a: TAge);
begin
    writeln('age = ', a);
end;

begin
    showAge(999);
end.
