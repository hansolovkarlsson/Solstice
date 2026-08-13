program TestConstTypedRecordWrongorder;

{ Fields out of declaration order must be a clear compile error, not a
  silently wrong assignment - typed record constants aren't a named-in-
  any-order struct literal, they must match the record type's own
  declared field order (matching real Delphi). }

type
    TPoint = record
        x, y: integer;
    end;

const
    Origin: TPoint = (y: 2; x: 1);

begin
end.
