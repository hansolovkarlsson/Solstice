program TestRecordParamBadArrayField;
type
    TArrRec = record
        vals: array[1..3] of integer;
    end;

procedure Foo(r: TArrRec);
begin
    writeln(r.vals[1]);
end;

begin
end.
