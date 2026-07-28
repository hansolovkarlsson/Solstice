program FuncDiscard;
var
    counter: integer;

function bump: integer;
begin
    counter := counter + 1;
    bump := counter;
end;

begin
    counter := 0;
    bump;             { called as a statement - return value discarded }
    bump;
    bump;
    writeln('counter after 3 discarded calls: ', counter);
    writeln('one more, using the value: ', bump);
    writeln('stack still sane: ', 1 + 2 + 3);
end.
