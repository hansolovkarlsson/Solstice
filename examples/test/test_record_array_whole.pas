program TestRecordArrayWhole;
type
    TPoint = record
        x: integer;
        y: integer;
    end;
    TStudent = record
        id: integer;
        scores: array[1..3] of integer;
    end;
var
    a, b: TPoint;
    s: TStudent;
    i: integer;
begin
    { array field }
    s.id := 42;
    s.scores[1] := 90;
    s.scores[2] := 85;
    s.scores[3] := 95;
    for i := 1 to 3 do
        write(s.scores[i], ' ');
    writeln;

    { whole-record assignment }
    a.x := 10;
    a.y := 20;
    b := a;
    writeln('b.x = ', b.x, ' b.y = ', b.y);

    { mutate a, confirm b is independent (a real copy, not aliased) }
    a.x := 999;
    writeln('after mutating a: a.x = ', a.x, ' b.x = ', b.x);
end.
