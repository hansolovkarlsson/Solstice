program TestPtrFields;
{ A record field's normal validation still applies through a pointer
  dereference - a subrange field's bounds are enforced, and a char field
  still requires exactly one character, exactly as if reached without a
  pointer at all. Expected output:
  A 30 }
type
    TAge = 0..150;
    PEntry = ^TEntry;
    TEntry = record
        initial: char;
        age: TAge;
    end;
var
    e: PEntry;
begin
    new(e);
    e^.initial := 'A';
    e^.age := 30;
    writeln(e^.initial, ' ', e^.age);
    dispose(e);
end.
