program TestDynArrayCopyNilSource;
var
    a, b: array of integer;
begin
    { a is never SetLength'd - Copy on a nil/unallocated array must not
      crash, and must produce another nil/empty array. }
    b := Copy(a);
    writeln(Length(b));    { 0 }
    writeln(b = nil);      { TRUE }

    b := Copy(a, 0, 5);
    writeln(Length(b));    { 0 }
    writeln(b = nil);      { TRUE }
end.
