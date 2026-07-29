program TestRealBasic;
var
    x, y, z: real;
    i: integer;
begin
    x := 3.14;
    writeln('x = ', x);

    y := 2.0;
    z := x + y;
    writeln('x + y = ', z);
    writeln('x - y = ', x - y);
    writeln('x * y = ', x * y);
    writeln('x / y = ', x / y);

    { / always produces real, even int/int }
    writeln('5 / 2 = ', 5 / 2);        { 2.5, NOT 2 }
    writeln('10 div 3 = ', 10 div 3);  { 3, integer division still works }

    { mixed int/real arithmetic }
    i := 3;
    writeln('i + x = ', i + x);        { 3 + 3.14 = 6.14 }
    writeln('x + i = ', x + i);

    { assignment widening }
    x := 5;
    writeln('x after x:=5 = ', x);     { 5, widened to real }
end.
