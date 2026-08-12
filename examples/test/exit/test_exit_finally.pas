program TestExitFinally;
{ 'exit;' inside a try/finally still runs the finally block on its way
  out, matching real Pascal. Expected output: try, finally, done. }
procedure Foo;
begin
    try
        writeln('try');
        exit;
        writeln('unreached');
    finally
        writeln('finally');
    end;
end;
begin
    Foo;
    writeln('done');
end.
