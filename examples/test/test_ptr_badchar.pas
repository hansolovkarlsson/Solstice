program TestPtrBadchar;
{ A char-typed field reached through a pointer must still be validated
  at runtime as exactly one character (OP_STORE_HEAP_FIELD_CHAR, chosen
  by codegen over the plain variant) - a clean Runtime Error, not a
  silently truncated or multi-character value. }
type
    PEntry = ^TEntry;
    TEntry = record
        initial: char;
    end;
var
    e: PEntry;
begin
    new(e);
    e^.initial := 'AB';
end.
