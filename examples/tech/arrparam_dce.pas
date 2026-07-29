program ArrParamDce;
var
    used: array[1..2] of integer;
    dead: array[1..2] of integer;

function firstOf(arr: array[1..2] of integer): integer;
begin
    firstOf := arr[1];
end;

begin
    used[1] := 5;
    dead[1] := 999;   { dead[] is never passed anywhere - should be eliminated }
    writeln(firstOf(used));
end.
