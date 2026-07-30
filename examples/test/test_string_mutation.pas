program TestStringMutation;
var s: string; i: integer;

procedure mutateLocal;
var local_s: string;
begin
    local_s := 'World';
    local_s[1] := 'w';
    writeln('local: ', local_s);
end;

begin
    s := 'Hello';
    writeln('before: ', s);
    s[1] := 'J';
    writeln('after s[1]:=J: ', s);

    { modify multiple characters, one at a time }
    for i := 1 to length(s) do
        s[i] := upcase(s[i]);
    writeln('all upper: ', s);

    { read after write, in an expression }
    if s[1] = 'J' then writeln('s[1] correctly reads back as J');

    mutateLocal;
end.
