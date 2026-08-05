program TestRecarrBadtype;
{ Whole-element copy between two arrays of DIFFERENT record types must be
  a Compile Error, not a silent field-by-field mismatch. The same check
  guards the other two supported copy directions (array-to-plain-record,
  plain-record-to-array) - only one direction is exercised here. }
type
    TA = record
        x: integer;
    end;
    TB = record
        y: integer;
    end;
var
    a: array[1..2] of TA;
    b: array[1..2] of TB;
begin
    a[1] := b[1];
end.
