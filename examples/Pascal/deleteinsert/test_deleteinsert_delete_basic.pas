program TestDeleteinsertDeleteBasic;
var
    s: string;
begin
    s := 'Hello, World!';
    Delete(s, 6, 7);
    writeln(s);

    s := 'abcdef';
    Delete(s, 1, 1);
    writeln(s);

    s := 'abcdef';
    Delete(s, 6, 1);
    writeln(s);
end.
