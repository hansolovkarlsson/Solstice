program TestConstTypedRecordExtra;

{ Too many fields (an extra one after every real field has been given)
  must be a clear compile error. }

type
    TPoint = record
        x, y: integer;
    end;

const
    Origin: TPoint = (x: 1; y: 2; z: 3);

begin
end.
