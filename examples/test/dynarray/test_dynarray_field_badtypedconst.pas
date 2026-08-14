program TestDynArrayFieldBadTypedConst;
type
    TBox = record
        data: array of integer;
    end;
const
    Bad: TBox = (data: nil);
begin
end.
