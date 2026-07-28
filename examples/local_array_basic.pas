program LocalArrayBasic;
var i, j: integer;

procedure bubbleSort;
var
    data: array[1..5] of integer;
    temp: integer;
begin
    data[1] := 5;
    data[2] := 3;
    data[3] := 4;
    data[4] := 1;
    data[5] := 2;

    for i := 1 to 4 do
        for j := 1 to 5 - i do
            if data[j] > data[j + 1] then begin
                temp := data[j];
                data[j] := data[j + 1];
                data[j + 1] := temp;
            end;

    for i := 1 to 5 do
        write(data[i], ' ');
    writeln;
end;

begin
    bubbleSort;
end.
