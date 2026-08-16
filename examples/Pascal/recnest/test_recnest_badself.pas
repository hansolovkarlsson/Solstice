program TestRecnestBadself;
{ Expected: Compile Error - a record type can't reference its own (still
  incomplete) type as a field, so this falls through to the ordinary
  "unknown type" error }
type
    TNode = record
        next: TNode;
        value: integer;
    end;
var
    n: TNode;
begin
end.
