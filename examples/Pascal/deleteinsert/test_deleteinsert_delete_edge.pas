program TestDeleteinsertDeleteEdge;
var
    s: string;
begin
    s := 'abcdef';
    Delete(s, 4, 100);   { count past end -> 'abc' }
    writeln(s);

    s := 'abcdef';
    Delete(s, 100, 2);   { index past end -> no-op }
    writeln(s);

    s := 'abcdef';
    Delete(s, -5, 3);    { index<1 clamps to 1 -> 'def' }
    writeln(s);

    s := 'abcdef';
    Delete(s, 3, 0);     { count<=0 -> no-op }
    writeln(s);
end.
