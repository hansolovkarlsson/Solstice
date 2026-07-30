program DocMathExample;
var radius, area: real;
begin
    radius := 3.0;
    area := pi * sqr(radius);
    writeln('area: ', area:0:2);

    writeln('sqrt(2): ', sqrt(2):0:5);
    writeln('2 ** 10: ', 2 ** 10);
    writeln('power(2, 0.5): ', power(2, 0.5):0:5);
end.
