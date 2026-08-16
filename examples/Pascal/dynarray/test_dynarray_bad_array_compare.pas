program TestDynarrayBadArrayCompare;

{ arr1 = arr2 (two same-typed dynamic arrays, no nil involved) still
  fails exactly as it does today - this feature must not accidentally
  loosen that separate, still-open gap (general dynamic-array-to-array
  comparison isn't part of the 'nil literal' bullet this closes). }

var
    arr1, arr2: array of integer;

begin
    if arr1 = arr2 then writeln('should not compile');
end.
