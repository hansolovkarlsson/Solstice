program TestConstTypedRecordMissing;

{ Too few fields (y is missing entirely) must be a clear compile error -
  every field is required, no partial initialization/defaults. }

type
    TPoint = record
        x, y: integer;
    end;

const
    Origin: TPoint = (x: 1);

begin
end.
