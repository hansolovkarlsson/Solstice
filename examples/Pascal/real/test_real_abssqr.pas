program TestRealAbsSqr;
var x, y: real; i: integer;
begin
    x := -3.5;
    writeln('abs(-3.5) = ', abs(x));      { 3.5 }
    writeln('sqr(-3.5) = ', sqr(x));      { 12.25 }

    y := 2.5;
    writeln('sqr(2.5) = ', sqr(y));       { 6.25 }

    { integer abs/sqr still work (regression) }
    i := -7;
    writeln('abs(-7) = ', abs(i));        { 7 }
    writeln('sqr(-7) = ', sqr(i));        { 49 }

    { direct literal }
    writeln('abs(-1.5) = ', abs(-1.5));
    writeln('sqr(-1.5) = ', sqr(-1.5));
end.
