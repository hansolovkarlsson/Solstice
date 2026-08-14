program TestDynArrayFieldBadTypedConst;
{ A dynamic-array field's typed-constant value must be an array literal
  ('[...]') - nothing else, including 'nil', is accepted (see
  test_dynarray_field_typedconst_basic.pas for the supported form). }
type
    TBox = record
        data: array of integer;
    end;
const
    Bad: TBox = (data: nil);
begin
end.
