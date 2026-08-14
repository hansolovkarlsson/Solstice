program TestDynarrayNilBadOrdering;

{ 'arr < nil' - still correctly rejected, matching the existing
  pointer-ordering restriction (only '=' and '<>' are defined). }

var
    arr: array of integer;

begin
    if arr < nil then writeln('should not compile');
end.
