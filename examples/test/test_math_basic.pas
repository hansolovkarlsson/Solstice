program TestMathBasic;
var x: real;
begin
    writeln('sqrt(16) = ', sqrt(16.0));       { 4 }
    writeln('sqrt(2) = ', sqrt(2));           { integer arg widens: 1.41421 }
    writeln('sin(0) = ', sin(0.0));           { 0 }
    writeln('cos(0) = ', cos(0.0));           { 1 }
    writeln('exp(0) = ', exp(0.0));           { 1 }
    writeln('exp(1) = ', exp(1.0));           { 2.71828 }
    writeln('ln(1) = ', ln(1.0));             { 0 }
    x := exp(1.0);
    writeln('ln(e) = ', ln(x));               { 1 }
    writeln('arctan(0) = ', arctan(0.0));     { 0 }
    writeln('pi = ', pi);                     { 3.14159 }
    writeln('sin(pi) approx 0 = ', sin(pi));  { very close to 0 }
end.
