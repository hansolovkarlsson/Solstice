program TestSizeofBadRecord;
type
    TRec = record
        name: string;
    end;
begin
    writeln(sizeOf(TRec));
end.
