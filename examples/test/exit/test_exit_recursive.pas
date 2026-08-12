program TestExitRecursive;
{ 'exit(value);' as a recursive function's base case - confirms it only
  unwinds the CURRENT call frame, regardless of recursion depth.
  Expected output: 120. }
function Fact(n: integer): integer;
begin
    if n <= 1 then
        exit(1);
    Fact := n * Fact(n - 1);
end;
begin
    writeln(Fact(5));
end.
