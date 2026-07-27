program ConcatTest;
var
    first, last, full: string;
    greeting: string;
begin
    first := 'Hello';
    last := 'World';
    full := first + ', ' + last + '!';
    writeln(full);

    greeting := 'Hi';
    if greeting + '!' = 'Hi!' then
        writeln('concat comparison works');
end.
