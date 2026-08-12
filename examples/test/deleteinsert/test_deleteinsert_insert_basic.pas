program TestDeleteinsertInsertBasic;
var
    s: string;
begin
    s := 'Hello!';
    Insert(' there', s, 6);
    writeln(s);

    s := 'abc';
    Insert('XY', s, 2);
    writeln(s);
end.
