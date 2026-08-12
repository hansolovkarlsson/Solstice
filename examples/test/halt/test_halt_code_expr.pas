program TestHaltCodeExpr;
{ 'halt(n);' accepts a full runtime expression, not just a literal -
  matching this compiler's general philosophy of evaluating real
  expressions rather than restricting to compile-time constants.
  Expected: no output, process exit code 17 (10 + 7). }
var x: integer;
begin
    x := 10;
    halt(x + 7);
end.
