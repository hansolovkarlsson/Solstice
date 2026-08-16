program TestArray2DLocal;
procedure localTest;
var
    temp: array[1..2, 1..2] of integer;
begin
    temp[1, 1] := 100;
    temp[2, 2] := 200;
    writeln('temp[1,1]=', temp[1, 1], ' temp[2,2]=', temp[2, 2]);
end;

begin
    localTest;
end.
