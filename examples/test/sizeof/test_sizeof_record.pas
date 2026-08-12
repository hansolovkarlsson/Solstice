program TestSizeofRecord;
type
    TInner = record
        a: byte;
        b: word;
    end;
    TRec = record
        flag: byte;
        delta: shortint;
        count: word;
        total: integer;
        inner: TInner;
    end;
begin
    writeln(sizeOf(TInner));
    writeln(sizeOf(TRec));
end.
