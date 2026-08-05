program TestPtrBadreadln;
{ readln into a pointer isn't meaningful (Pascal doesn't parse a pointer
  value from text) - must be a Compile Error. }
type
    PInt = ^integer;
var
    p: PInt;
begin
    readln(p);
end.
