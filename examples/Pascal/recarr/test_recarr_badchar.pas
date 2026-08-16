program TestRecarrBadchar;
{ A char-typed field of a record-array element must still be validated at
  runtime as exactly one character (OP_STORE_ARRAY_RECORD_FIELD_CHAR,
  chosen by codegen over the plain variant - see NODE_ARRAY_RECORD_FIELD_
  ASSIGN in codegen.c) - a clean Runtime Error, not a silently truncated
  or multi-character value. }
type
    TEntry = record
        initial: char;
    end;
var
    entries: array[1..2] of TEntry;
begin
    entries[1].initial := 'AB';
end.
