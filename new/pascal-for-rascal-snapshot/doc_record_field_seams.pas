program DocRecordFieldSeams;
type TCounter = record n: integer; end;
var c: TCounter;
begin
    c.n := 0;
    inc(c.n);
    for c.n := 1 to 3 do
        writeln('counting: ', c.n);
    readln(c.n);
    writeln('read: ', c.n);
end.
