program TestSubrangeBasic;
type
    TAge = 0..150;
var
    a: TAge;
begin
    a := 30;
    writeln('a = ', a);
    a := 0;
    writeln('a = ', a);
    a := 150;
    writeln('a = ', a);
end.
