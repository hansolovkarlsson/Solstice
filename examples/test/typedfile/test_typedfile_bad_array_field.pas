program TestTypedfileBadArray;
type
    TRecord = record
        arr: array[1..3] of integer;
    end;
var
    f: file of TRecord;
begin
end.
