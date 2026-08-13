program TestConstTypedRecordNestedfield;

{ A record type with a nested-record-typed field must be rejected UP
  FRONT, clearly, as soon as the typed constant's own record type is
  recognized - v1 scope is all-scalar record types only. }

type
    TInner = record
        n: integer;
    end;
    TOuter = record
        inner: TInner;
    end;

const
    Bad: TOuter = (inner: 1);

begin
end.
