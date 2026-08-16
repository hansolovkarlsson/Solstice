program TestAliasDynArrayCrossCompat;
type
    TIntArray = array of integer;
var
    direct: array of integer;
    aliased: TIntArray;
begin
    { An alias-declared and a directly-declared 'array of integer' are the
      SAME structurally-deduped dynamic-array shape - freely
      interchangeable, exactly like TAge/integer already are for scalars. }
    SetLength(direct, 2);
    direct[0] := 1;
    direct[1] := 2;
    aliased := direct;
    writeln(aliased[0], ' ', aliased[1]);  { 1 2 }
end.
