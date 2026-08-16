program TestExitBare;
{ Bare 'exit;' mid-procedure skips the remaining statements in the body.
  Expected output: a, c (not b). }
procedure Foo;
begin
    writeln('a');
    exit;
    writeln('b');
end;
begin
    Foo;
    writeln('c');
end.
