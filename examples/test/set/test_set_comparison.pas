program TestSetComparison;
var
    a, b, c: set of 0..15;
    i: integer;
begin
    a := [1, 2, 3, 4];
    b := [3, 4, 5, 6];

    c := a * b;
    writeln('intersection: ', 3 in c, ' ', 1 in c); { TRUE FALSE }

    c := a + b;
    writeln('union: ', 1 in c, ' ', 5 in c, ' ', 10 in c); { TRUE TRUE FALSE }

    c := a - b;
    writeln('diff: ', 1 in c, ' ', 3 in c); { TRUE FALSE }

    writeln('a=a: ', a = a);   { TRUE }
    writeln('a=b: ', a = b);   { FALSE }
    writeln('a<>b: ', a <> b); { TRUE }

    a := [1, 2];
    b := [1, 2, 3];
    writeln('a<=b: ', a <= b); { TRUE  - subset }
    writeln('b<=a: ', b <= a); { FALSE }
    writeln('b>=a: ', b >= a); { TRUE  - superset }
    writeln('a>=b: ', a >= b); { FALSE }
    writeln('a<=a: ', a <= a); { TRUE  - equal counts as subset/superset }
end.
