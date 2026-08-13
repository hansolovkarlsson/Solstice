program TestConstTypedArrBadRecord;
const
    { Record typed constants ARE supported now (see
      test_const_typed_record_basic.pas), but only if the record type is
      declared BEFORE the const that uses it - const/type sections can
      interleave, but ordering still matters (see
      docs/LANGUAGE.md#program-structure). Here TPoint is declared
      AFTER Origin tries to use it, so TPoint isn't recognized as a
      record type (or anything else) yet at this point - falls through
      to the generic "expected 'array' or a record type name" message,
      the same one a genuinely unknown type would get. }
    Origin: TPoint = (X, Y);
type
    TPoint = record
        X, Y: integer;
    end;
begin
end.
