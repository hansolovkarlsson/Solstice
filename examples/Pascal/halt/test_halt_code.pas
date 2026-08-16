program TestHaltCode;
{ 'halt(n);' with a literal propagates n as the actual OS process exit
  code (check with $? after running under solvm). Expected: prints
  'bye', process exit code 42. }
begin
    writeln('bye');
    halt(42);
end.
