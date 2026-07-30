program TestConstBasic;
const
    MaxSize = 100;
    Pi2 = 3.14159 * 2;
    Greeting = 'Hello';
    Initial = 'A';
    Enabled = true;
    Neg = -5;
    Doubled = MaxSize * 2;
var
    i: integer;
begin
    writeln('MaxSize = ', MaxSize);
    writeln('Pi2 = ', Pi2:0:5);
    writeln('Greeting = ', Greeting);
    writeln('Initial = ', Initial);
    writeln('Enabled = ', Enabled);
    writeln('Neg = ', Neg);
    writeln('Doubled = ', Doubled);
    for i := 1 to 3 do
        writeln('MaxSize + i = ', MaxSize + i);
end.
