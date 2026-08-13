program TestConstTypedRecordBadrange;

{ A 'byte' field's initializer value out of range (300 > 255) is a
  RUNTIME error at program start, not a compile-time one - the exact
  same range-check ordinary assignment already performs, matching
  test_const_typed_array_badrange.pas's own array-element equivalent
  (see wrap_range_check()'s reuse in parse_typed_const_record_declaration()). }

type
    TPerson = record
        age: byte;
    end;

const
    Bad: TPerson = (age: 300);

begin
end.
