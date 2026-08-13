program TestConstTypedArrBadRecord;
const
    { record typed constants aren't supported: 'const' is always parsed
      before 'type' in this compiler, so a record type - always
      declared in 'type' - doesn't exist yet at this point regardless
      of where it appears in the source }
    Origin: TPoint = (X, Y);
type
    TPoint = record
        X, Y: integer;
    end;
begin
end.
