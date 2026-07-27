program StringTest;
var
    greeting: string;
    name: string;
    same: boolean;
begin
    greeting := 'Hello';
    name := 'World';
    writeln(greeting);
    writeln(name);
    writeln('Hello, World!');

    same := greeting = 'Hello';
    writeln(same);

    same := greeting = name;
    writeln(same);

    if name <> 'World' then
        writeln('mismatch')
    else
        writeln('match');
end.
