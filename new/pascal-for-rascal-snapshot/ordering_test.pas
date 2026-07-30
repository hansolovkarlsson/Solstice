program OrderingTest;
var
    a, b: string;
begin
    a := 'apple';
    b := 'banana';

    if a < b then writeln('apple < banana: true')
    else writeln('apple < banana: false');

    if a > b then writeln('apple > banana: true')
    else writeln('apple > banana: false');

    if a <= 'apple' then writeln('apple <= apple: true');
    if a >= 'apple' then writeln('apple >= apple: true');

    writeln('sorted check: ', ('apple' < 'banana'), ' ', ('zebra' < 'apple'));
end.
