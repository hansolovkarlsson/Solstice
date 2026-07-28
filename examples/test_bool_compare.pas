program TestBoolCompare;
var a, b: boolean;
begin
    a := true;
    b := false;

    if a = b then writeln('a = b: true') else writeln('a = b: false');
    if a <> b then writeln('a <> b: true') else writeln('a <> b: false');
    if a = a then writeln('a = a: true') else writeln('a = a: false');

    if b < a then writeln('false < true: true') else writeln('false < true: false');
    if a > b then writeln('true > false: true') else writeln('true > false: false');
    if a <= a then writeln('a <= a: true') else writeln('a <= a: false');
    if b >= a then writeln('false >= true: true') else writeln('false >= true: false');

    writeln('a is :',a);
    writeln('b is :',b);
end.
