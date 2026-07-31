program TestRecordCompareBadArray;
type
    TStudent = record
        name: string;
        scores: array[1..3] of integer;
    end;
var
    a, b: TStudent;
begin
    if a = b then writeln('same');
end.
