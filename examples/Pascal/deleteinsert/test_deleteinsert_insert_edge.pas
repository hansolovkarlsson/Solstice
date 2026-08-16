program TestDeleteinsertInsertEdge;
var
    s: string;
begin
    s := 'abc';
    Insert('XY', s, 1);      { prepend -> 'XYabc' }
    writeln(s);

    s := 'abc';
    Insert('XY', s, 4);      { index = len+1 -> append -> 'abcXY' }
    writeln(s);

    s := 'abc';
    Insert('XY', s, 100);    { index far past end -> append -> 'abcXY' }
    writeln(s);

    s := 'abc';
    Insert('XY', s, -5);     { index<1 -> prepend -> 'XYabc' }
    writeln(s);
end.
