program DocStringExample;
var
    name: string;
    initial: char;
begin
    name := 'Ada Lovelace';
    writeln('length: ', length(name));
    writeln('first char: ', name[1]);
    writeln('uppercase: ', uppercase(name));
    writeln('surname: ', copy(name, pos(' ', name) + 1, length(name)));
    initial := upcase(name[1]);
    writeln('initial: ', initial);
end.
