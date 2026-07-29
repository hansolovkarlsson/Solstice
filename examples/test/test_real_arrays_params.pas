program TestRealArraysParams;
var
    scores: array[1..3] of real;
    i: integer;
    total: real;

function average(arr: array[1..3] of real): real;
var
    s: real;
begin
    s := 0.0;
    for i := 1 to 3 do
        s := s + arr[i];
    average := s / 3;
end;

procedure doubleIt(v: real);
var local_r: real;
begin
    local_r := v * 2;
    writeln('doubled: ', local_r);
end;

begin
    scores[1] := 1.5;
    scores[2] := 2.5;
    scores[3] := 3.5;

    for i := 1 to 3 do
        write(scores[i], ' ');
    writeln;

    total := average(scores);
    writeln('average = ', total);

    { widening when passing an int literal argument to a real parameter }
    doubleIt(4);
    doubleIt(2.5);
end.
