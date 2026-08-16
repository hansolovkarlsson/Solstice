program TestRealCompareTrunc;
var
    a, b: real;
    i: integer;
begin
    a := 3.5;
    b := 3.5;
    if a = b then writeln('a = b: true');
    if a <> b then writeln('a <> b: true') else writeln('a <> b: false');

    b := 2.1;
    if a > b then writeln('a > b: true');
    if b < a then writeln('b < a: true');
    if a >= b then writeln('a >= b: true');
    if b <= a then writeln('b <= a: true');

    { mixed int/real comparison }
    i := 3;
    if i < a then writeln('i < a: true');   { 3 < 3.5 }
    if a > i then writeln('a > i: true');

    writeln('trunc(3.7) = ', trunc(3.7));    { 3 }
    writeln('trunc(-3.7) = ', trunc(-3.7));  { -3, truncates toward zero }
    writeln('round(3.5) = ', round(3.5));    { 4 }
    writeln('round(3.4) = ', round(3.4));    { 3 }
    writeln('round(-3.5) = ', round(-3.5));  { -4, half away from zero }

    { unary minus on real }
    a := -a;
    writeln('negated a = ', a);
end.
