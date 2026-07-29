program WriteTest;
var number: integer;
begin
    number := 5;
    writeln('val:', number);

    write('a');
    write('b');
    writeln('c');

    writeln;

    writeln('mixed: ', 42, ' and ', true);

    write('no newline at end');
end.
