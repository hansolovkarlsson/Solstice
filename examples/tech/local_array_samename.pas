program LocalArraySameName;

procedure procA;
var temp: array[1..2] of integer;
begin
    temp[1] := 10;
    temp[2] := 20;
    writeln('A: ', temp[1], ' ', temp[2]);
end;

procedure procB;
var temp: array[1..2] of string;
begin
    temp[1] := 'hello';
    temp[2] := 'world';
    writeln('B: ', temp[1], ' ', temp[2]);
end;

begin
    procA;
    procB;
end.
