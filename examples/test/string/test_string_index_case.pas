program TestStringIndexCase;
var s: string; c: char; i: integer;

function firstChar(str: string): char;
var local_s: string;
begin
    local_s := str;
    firstChar := local_s[1];
end;

begin
    s := 'Pascal';
    c := s[1];
    writeln('s[1] = ', c);          { P }
    writeln('s[6] = ', s[6]);       { l }

    for i := 1 to length(s) do
        write(s[i]);
    writeln;                         { Pascal }

    writeln(uppercase(s));           { PASCAL }
    writeln(lowercase(s));           { pascal }
    writeln(upcase('a'));            { A }
    writeln(upcase('Z'));            { Z (unchanged) }
    writeln(upcase('5'));            { 5 (unchanged, not a letter) }

    writeln('local index: ', firstChar('World'));  { W }
end.
