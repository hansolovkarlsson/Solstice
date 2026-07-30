program TestConsolidation2;
type TFlags = record active: boolean; grade: char; end;
var
    f: TFlags;
    scores: array[1..3] of real;

function reverseString(s: string): string;
var i: integer; result: string;
begin
    result := s;
    for i := 1 to length(s) do
        result[i] := s[length(s) - i + 1];
    reverseString := result;
end;

function sumRealArray(arr: array[1..3] of real): real;
var i: integer; total: real;
begin
    total := 0.0;
    for i := 1 to 3 do
        total := total + arr[i];
    sumRealArray := total;
end;

begin
    { local for-loop + local string mutation across a function call }
    writeln(reverseString('Hello'));   { olleH }
    writeln(reverseString('Pascal'));  { lacsaP }

    { array-ref parameter with real elements }
    scores[1] := 1.5; scores[2] := 2.5; scores[3] := 3.0;
    writeln('sum: ', sumRealArray(scores):0:1);  { 7.0 }

    { boolean/char fields + logical ops + ord/chr }
    f.active := true;
    f.grade := 'B';
    if f.active and (f.grade <> 'F') then
        writeln('passing, grade ord = ', ord(f.grade));
    f.grade := chr(ord(f.grade) + 1);
    writeln('bumped grade: ', f.grade);
end.
