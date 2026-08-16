program TestPtrBasic;
{ Basic pointer-to-scalar: new/dispose, '^' as a whole-value dereference
  (read and write) - no record involved. Expected output:
  5
  10 }
type
    PInt = ^integer;
var
    p: PInt;
begin
    new(p);
    p^ := 5;
    writeln(p^);
    p^ := p^ * 2;
    writeln(p^);
    dispose(p);
end.
