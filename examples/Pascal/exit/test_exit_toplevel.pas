program TestExitToplevel;
{ Bare 'exit;' is also legal in the main program body - stops the program
  right there. Expected output: a (not b). }
begin
    writeln('a');
    exit;
    writeln('b');
end.
