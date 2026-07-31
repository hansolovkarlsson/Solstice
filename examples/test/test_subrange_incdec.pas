program TestSubrangeIncDec;
type
    TAge = 0..150;
var
    a: TAge;
begin
    a := 149;
    inc(a);
    writeln('a = ', a);
    a := 1;
    dec(a);
    writeln('a = ', a);
end.
