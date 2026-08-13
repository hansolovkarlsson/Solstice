program TestConstTypedRecordBadvalue;

{ A field's initializer value that's not a compile-time-constant
  expression must be rejected, same as an array element's own value
  (see parse_typed_const_value()). }

var
    n: integer;

type
    TPoint = record
        x, y: integer;
    end;

const
    Origin: TPoint = (x: n; y: 2);

begin
end.
