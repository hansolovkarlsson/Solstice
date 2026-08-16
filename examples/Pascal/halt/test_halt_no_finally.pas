program TestHaltNoFinally;
{ Unlike 'exit', 'halt' does NOT run enclosing finally blocks - matches
  real Pascal/Delphi. Expected output: try (finally-should-not-print
  must NOT appear, and neither should 'unreached'). }
procedure Foo;
begin
    try
        writeln('try');
        halt;
    finally
        writeln('finally-should-not-print');
    end;
end;
begin
    Foo;
    writeln('unreached');
end.
