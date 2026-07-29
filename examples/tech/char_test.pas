program CharTest;
var
    c1, c2: char;
    s: string;
    letters: array[1..5] of char;
    i: integer;
begin
    c1 := 'a';
    c2 := 'b';
    writeln(c1);
    writeln(c2);

    if c1 < c2 then writeln('a < b: true');
    if c1 = 'a' then writeln('c1 = a: true');
    if c1 <> c2 then writeln('c1 <> c2: true');

    s := c1 + c2;
    writeln('concat: ', s);

    s := 'x' + c1;
    writeln('mixed concat: ', s);

    letters[1] := 'H';
    letters[2] := 'e';
    letters[3] := 'l';
    letters[4] := 'l';
    letters[5] := 'o';
    for i := 1 to 5 do
        write(letters[i]);
    writeln;

    c1 := s;  { will fail at runtime: 's' now holds "xa", length 2 }
end.
