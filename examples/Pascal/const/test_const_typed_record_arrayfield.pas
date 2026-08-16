program TestConstTypedRecordArrayfield;

{ A record type with an array-typed field must be rejected UP FRONT,
  clearly, as soon as the typed constant's own record type is
  recognized - v1 scope is all-scalar record types only. }

type
    TScores = record
        values: array[1..3] of integer;
    end;

const
    Bad: TScores = (values: 1);

begin
end.
