program TestDynarrayNilNeverinit;

{ A never-SetLength'd array compares equal to nil - confirms the
  zero-init default already satisfies this, no assignment needed. yes }

var
    arr: array of integer;

begin
    if arr = nil then writeln('yes') else writeln('no');
end.
