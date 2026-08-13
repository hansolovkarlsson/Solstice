program TestConstTypedRecordSubrange;

{ A record typed constant with a 'byte' (subrange) field - confirms the
  range-check machinery wrap_range_check() already gives every ordinary
  assignment also applies to a record typed constant's own field
  initializers, via each field's own is_subrange/subrange_lower/upper.
  age=200 }

type
    TPerson = record
        age: byte;
    end;

const
    Old: TPerson = (age: 200);

begin
    writeln('age=', Old.age);
end.
