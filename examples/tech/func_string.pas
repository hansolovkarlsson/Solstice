program FuncString;
var s: string;

function describe(n: integer): string;
begin
    if n mod 2 = 0 then
        describe := 'even'
    else
        describe := 'odd';
end;

begin
    s := describe(4) + ' and ' + describe(7);
    writeln(s);
end.
