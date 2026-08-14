program TestAliasDynArrayChain;
type
    TA = array of integer;
    TB = TA;    { alias of an alias, same as an ordinary scalar alias chain }
var
    x: TB;
begin
    SetLength(x, 2);
    x[0] := 1;
    x[1] := 2;
    writeln(x[0], ' ', x[1]);
end.
