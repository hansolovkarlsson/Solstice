program TestConstTypedRecordBadassign;

{ Assigning to a record typed constant's field must be a compile-time
  error, exactly like an array typed constant's element (see
  test_const_typed_array_badassign.pas) - each field's own leaf global
  symbol is marked is_const, and field assignment already resolves down
  to that same leaf symbol via parse_global_assignment(). }

type
    TPoint = record
        x, y: integer;
    end;

const
    Origin: TPoint = (x: 1; y: 2);

begin
    Origin.x := 9;
end.
