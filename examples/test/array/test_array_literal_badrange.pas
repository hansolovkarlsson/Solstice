program TestArrayLiteralBadRange;
{ An array literal's own element values are range-checked exactly like
  an ordinary indexed assignment - an out-of-range byte element is a
  RUNTIME error, not a compile-time one (same generic wrap_range_check()
  mechanism either way). }
var
    arr: array[1..2] of byte;
begin
    arr := [1, 300];
    writeln('unreachable');
end.
